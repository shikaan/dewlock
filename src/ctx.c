#include "pool-buffer.h"
#include <assert.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static int anonymous_shm_open(void) {
  int retries = 100;

  do {
    // try a probably-unique name
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pid_t pid = getpid();
    char name[50];
    snprintf(name, sizeof(name), "/dewlock-%x-%x", (unsigned int)pid,
             (unsigned int)ts.tv_nsec);

    // shm_open guarantees that O_CLOEXEC is set
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink(name);
      return fd;
    }

    --retries;
  } while (retries > 0 && errno == EEXIST);

  return -1;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
  (void)wl_buffer;
  pool_buffer_t *buffer = data;
  buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {.release =
                                                              buffer_release};

pool_buffer_t *create_buffer(struct wl_shm *shm, pool_buffer_t *buf,
                             int32_t width, int32_t height, uint32_t format) {
  assert(shm && "shm must be non-null");
  assert(buf && "buf must be non-null");
  assert(width >= 0 && "width must be non-negative");
  assert(height >= 0 && "height must be non-negative");
  uint32_t stride = (uint32_t)width * 4;
  size_t size = stride * (uint32_t)height;

  void *data = NULL;
  if (size > 0) {
    int fd = anonymous_shm_open();
    if (fd == -1) {
      return NULL;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
      close(fd);
      return NULL;
    }
    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    buf->buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                            (int32_t)stride, format);
    wl_buffer_add_listener(buf->buffer, &buffer_listener, buf);
    wl_shm_pool_destroy(pool);
    close(fd);
  }

  buf->size = size;
  buf->width = (uint32_t)width;
  buf->height = (uint32_t)height;
  buf->data = data;
  buf->surface = cairo_image_surface_create_for_data(
      data, CAIRO_FORMAT_ARGB32, width, height, (int)stride);
  buf->cairo = cairo_create(buf->surface);
  return buf;
}

void destroy_buffer(pool_buffer_t *buffer) {
  assert(buffer && "buffer must be non-null");
  if (buffer->buffer) {
    wl_buffer_destroy(buffer->buffer);
  }
  if (buffer->cairo) {
    cairo_destroy(buffer->cairo);
  }
  if (buffer->surface) {
    cairo_surface_destroy(buffer->surface);
  }
  if (buffer->data) {
    munmap(buffer->data, buffer->size);
  }
  memset(buffer, 0, sizeof(pool_buffer_t));
}

pool_buffer_t *get_next_buffer(struct wl_shm *shm, pool_buffer_t pool[static 2],
                               uint32_t width, uint32_t height) {
  pool_buffer_t *buffer = NULL;

  for (size_t i = 0; i < 2; ++i) {
    if (pool[i].busy) {
      continue;
    }
    buffer = &pool[i];
  }

  if (!buffer) {
    return NULL;
  }

  if (buffer->width != width || buffer->height != height) {
    destroy_buffer(buffer);
  }

  if (!buffer->buffer) {
    if (!create_buffer(shm, buffer, (int32_t)width, (int32_t)height,
                       WL_SHM_FORMAT_ARGB8888)) {
      return NULL;
    }
  }
  buffer->busy = true;
  return buffer;
}
