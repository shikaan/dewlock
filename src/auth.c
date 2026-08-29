#include "auth.h"
#include "log.h"
#include "result.h"
#include "safebuf.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int pipes[2][2] = {{-1, -1}, {-1, -1}};

static ssize_t read_full(int fd, void *dst, size_t size) {
  char *buf = dst;
  size_t offset = 0;
  while (offset < size) {
    ssize_t n = read(fd, &buf[offset], size - offset);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      log_error("read() failed: %s", strerror(errno));
      return -1;
    } else if (n == 0) {
      if (offset == 0) {
        return 0;
      }
      log_error("read() failed: unexpected EOF", NULL);
      return -1;
    }
    offset += (size_t)n;
  }
  return (ssize_t)offset;
}

static bool write_full(int fd, const void *src, size_t size) {
  const char *buf = src;
  size_t offset = 0;
  while (offset < size) {
    ssize_t n = write(fd, &buf[offset], size - offset);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      log_error("write() failed: %s", strerror(errno));
      return false;
    } else if (n == 0) {
      log_error("write() failed: unexpected short write", NULL);
      return false;
    }
    offset += (size_t)n;
  }
  return true;
}

result_t auth_read_request(sbuf_t *pw) {
  assert(pw && "pw must be non-null");
  int fd = pipes[0][0];

  size_t size;
  ssize_t n = read_full(fd, &size, sizeof(size));
  if (n == 0) {
    return ERR_AUTH_EOF;
  }
  if (n < 0) {
    return ERR_AUTH_READ;
  }
  if (size == 0) {
    log_error("received invalid pw check request: zero size", NULL);
    return ERR_AUTH_INVALID;
  }

  log_debug("received pw check request", NULL);

  result_t res = sbuf_create(pw, size);
  if (res != OK) {
    return res;
  }

  if (read_full(fd, pw->buf, size) <= 0) {
    log_error("failed to read pw: %s", strerror(errno));
    return ERR_AUTH_READ;
  }

  if (pw->buf[size - 1] != '\0') {
    log_error("received invalid pw check request: not NUL-terminated", NULL);
    return ERR_AUTH_INVALID;
  }
  pw->len = size - 1;
  return OK;
}

result_t auth_write_reply(bool success) {
  if (!write_full(pipes[1][1], &success, sizeof(success))) {
    return ERR_AUTH_WRITE;
  }
  return OK;
}

result_t auth_spawn_child(void) {
  if (pipe(pipes[0]) != 0) {
    log_error("failed to create pipe: %s", strerror(errno));
    return ERR_AUTH_PIPE;
  }
  if (pipe(pipes[1]) != 0) {
    log_error("failed to create pipe: %s", strerror(errno));
    return ERR_AUTH_PIPE;
  }
  pid_t child = fork();
  if (child < 0) {
    log_error("failed to fork: %s", strerror(errno));
    return ERR_AUTH_FORK;
  } else if (child == 0) {
    struct sigaction sa = {
        .sa_handler = SIG_IGN,
    };
    sigaction(SIGUSR1, &sa, NULL);
    close(pipes[0][1]);
    close(pipes[1][0]);
    auth_run();
  }
  close(pipes[0][0]);
  close(pipes[1][1]);
  return OK;
}

result_t auth_write_request(sbuf_t *pw) {
  assert(pw && "pw must be non-null");
  assert(pw->buf && "pw->buf must be non-null");
  result_t result = ERR_AUTH_WRITE;
  int fd = pipes[0][1];

  size_t size = pw->len + 1;
  if (!write_full(fd, &size, sizeof(size))) {
    log_error("failed to write pw size: %s", strerror(errno));
    goto out;
  }

  if (!write_full(fd, pw->buf, size)) {
    log_error("failed to write pw buffer: %s", strerror(errno));
    goto out;
  }

  result = OK;

out:
  sbuf_clear(pw);
  return result;
}

result_t auth_read_reply(bool *auth_success) {
  if (read_full(pipes[1][0], auth_success, sizeof(*auth_success)) <= 0) {
    log_error("failed to read pw result", NULL);
    return ERR_AUTH_READ;
  }
  return OK;
}

int auth_get_reply_fd(void) { return pipes[1][0]; }
