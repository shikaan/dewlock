#include "loop.h"
#include "log.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

struct loop_fd_event {
  void (*callback)(int fd, short mask, void *data);
  void *data;
  struct wl_list link; // struct loop_fd_event::link
};

struct loop_timer {
  void (*callback)(void *data);
  void *data;
  struct timespec expiry;
  bool removed;
  struct wl_list link; // struct loop_timer::link
};

struct loop {
  struct pollfd *fds;
  int fd_length;
  int fd_capacity;

  struct wl_list fd_events; // struct loop_fd_event::link
  struct wl_list timers;    // struct loop_timer::link
};

struct loop *loop_create(void) {
  struct loop *loop = calloc(1, sizeof(struct loop));
  if (!loop) {
    log_error("Unable to allocate memory for loop", NULL);
    return NULL;
  }
  loop->fd_capacity = 10;
  loop->fds = malloc(sizeof(struct pollfd) * (size_t)loop->fd_capacity);
  wl_list_init(&loop->fd_events);
  wl_list_init(&loop->timers);
  return loop;
}

void loop_destroy(struct loop *loop) {
  struct loop_fd_event *event = NULL, *tmp_event = NULL;
  wl_list_for_each_safe(event, tmp_event, &loop->fd_events, link) {
    wl_list_remove(&event->link);
    free(event);
  }
  struct loop_timer *timer = NULL, *tmp_timer = NULL;
  wl_list_for_each_safe(timer, tmp_timer, &loop->timers, link) {
    wl_list_remove(&timer->link);
    free(timer);
  }
  free(loop->fds);
  free(loop);
}

void loop_poll(struct loop *loop) {
  // Calculate next timer in ms
  int ms = INT_MAX;
  if (!wl_list_empty(&loop->timers)) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct loop_timer *timer = NULL;
    wl_list_for_each(timer, &loop->timers, link) {
      int timer_ms = (int)((timer->expiry.tv_sec - now.tv_sec) * 1000);
      timer_ms += (int)((timer->expiry.tv_nsec - now.tv_nsec) / 1000000);
      if (timer_ms < ms) {
        ms = timer_ms;
      }
    }
  }
  if (ms < 0) {
    ms = 0;
  }

  int ret = poll(loop->fds, (nfds_t)loop->fd_length, ms);
  if (ret < 0 && errno != EINTR) {
    log_error("poll failed: %s", strerror(errno));
    exit(1);
  }

  // Dispatch fds
  size_t fd_index = 0;
  struct loop_fd_event *event = NULL;
  wl_list_for_each(event, &loop->fd_events, link) {
    struct pollfd pfd = loop->fds[fd_index];

    // Always send these events
    unsigned events = (unsigned)pfd.events | POLLHUP | POLLERR;

    if ((unsigned)pfd.revents & events) {
      event->callback(pfd.fd, pfd.revents, event->data);
    }

    ++fd_index;
  }

  // Dispatch timers
  if (!wl_list_empty(&loop->timers)) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    struct loop_timer *timer = NULL, *tmp_timer = NULL;
    wl_list_for_each_safe(timer, tmp_timer, &loop->timers, link) {
      if (timer->removed) {
        wl_list_remove(&timer->link);
        free(timer);
        continue;
      }

      bool expired = timer->expiry.tv_sec < now.tv_sec ||
                     (timer->expiry.tv_sec == now.tv_sec &&
                      timer->expiry.tv_nsec < now.tv_nsec);
      if (expired) {
        timer->callback(timer->data);
        wl_list_remove(&timer->link);
        free(timer);
      }
    }
  }
}

void loop_add_fd(struct loop *loop, int fd, short mask,
                 void (*callback)(int fd, short mask, void *data), void *data) {
  assert(loop && "loop must be non-null");
  assert(callback && "callback must be non-null");
  struct loop_fd_event *event = calloc(1, sizeof(struct loop_fd_event));
  if (!event) {
    log_error("Unable to allocate memory for event", NULL);
    return;
  }
  event->callback = callback;
  event->data = data;
  wl_list_insert(loop->fd_events.prev, &event->link);

  struct pollfd pfd = {fd, mask, 0};

  if (loop->fd_length == loop->fd_capacity) {
    loop->fd_capacity += 10;
    loop->fds =
        realloc(loop->fds, sizeof(struct pollfd) * (size_t)loop->fd_capacity);
  }

  loop->fds[loop->fd_length++] = pfd;
}

struct loop_timer *loop_add_timer(struct loop *loop, int ms,
                                  void (*callback)(void *data), void *data) {
  assert(loop && "loop must be non-null");
  assert(callback && "callback must be non-null");
  struct loop_timer *timer = calloc(1, sizeof(struct loop_timer));
  if (!timer) {
    log_error("Unable to allocate memory for timer", NULL);
    return NULL;
  }
  timer->callback = callback;
  timer->data = data;

  clock_gettime(CLOCK_MONOTONIC, &timer->expiry);
  timer->expiry.tv_sec += ms / 1000;

  long int nsec = (ms % 1000) * 1000000;
  if (timer->expiry.tv_nsec + nsec >= 1000000000) {
    timer->expiry.tv_sec++;
    nsec -= 1000000000;
  }
  timer->expiry.tv_nsec += nsec;

  wl_list_insert(&loop->timers, &timer->link);

  return timer;
}

bool loop_remove_fd(struct loop *loop, int fd) {
  assert(loop && "loop must be non-null");
  size_t fd_index = 0;
  struct loop_fd_event *event = NULL, *tmp_event = NULL;
  wl_list_for_each_safe(event, tmp_event, &loop->fd_events, link) {
    if (loop->fds[fd_index].fd == fd) {
      wl_list_remove(&event->link);
      free(event);

      loop->fd_length--;
      memmove(&loop->fds[fd_index], &loop->fds[fd_index + 1],
              sizeof(struct pollfd) * ((size_t)loop->fd_length - fd_index));
      return true;
    }
    ++fd_index;
  }
  return false;
}

bool loop_remove_timer(struct loop *loop, struct loop_timer *remove) {
  assert(loop && "loop must be non-null");
  struct loop_timer *timer = NULL, *tmp_timer = NULL;
  wl_list_for_each_safe(timer, tmp_timer, &loop->timers, link) {
    if (timer == remove) {
      timer->removed = true;
      return true;
    }
  }
  return false;
}
