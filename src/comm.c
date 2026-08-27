#include "comm.h"
#include "dewlock.h"
#include "log.h"
#include "password-buffer.h"
#include "result.h"
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static int comm[2][2] = {{-1, -1}, {-1, -1}};

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

result_t read_comm_request(char **buf_ptr, size_t *size) {
  assert(buf_ptr && "buf_ptr must be non-null");
  assert(size && "size must be non-null");
  int fd = comm[0][0];

  ssize_t n = read_full(fd, size, sizeof(*size));
  if (n == 0) {
    return ERR_COMM_EOF;
  }
  if (n < 0) {
    return ERR_COMM_READ;
  }
  if (*size == 0) {
    log_error("received invalid pw check request: zero size", NULL);
    return ERR_COMM_INVALID;
  }

  log_debug("received pw check request", NULL);

  char *buf = password_buffer_create(*size);
  if (!buf) {
    return ERROR;
  }

  if (read_full(fd, buf, *size) <= 0) {
    log_error("failed to read pw: %s", strerror(errno));
    return ERR_COMM_READ;
  }

  if (buf[*size - 1] != '\0') {
    log_error("received invalid pw check request: not NUL-terminated", NULL);
    return ERR_COMM_INVALID;
  }
  *buf_ptr = buf;
  return OK;
}

result_t write_comm_reply(bool success) {
  if (!write_full(comm[1][1], &success, sizeof(success))) {
    return ERR_COMM_WRITE;
  }
  return OK;
}

result_t spawn_comm_child(void) {
  if (pipe(comm[0]) != 0) {
    log_error("failed to create pipe: %s", strerror(errno));
    return ERR_COMM_PIPE;
  }
  if (pipe(comm[1]) != 0) {
    log_error("failed to create pipe: %s", strerror(errno));
    return ERR_COMM_PIPE;
  }
  pid_t child = fork();
  if (child < 0) {
    log_error("failed to fork: %s", strerror(errno));
    return ERR_COMM_FORK;
  } else if (child == 0) {
    struct sigaction sa = {
        .sa_handler = SIG_IGN,
    };
    sigaction(SIGUSR1, &sa, NULL);
    close(comm[0][1]);
    close(comm[1][0]);
    run_pw_backend_child();
  }
  close(comm[0][0]);
  close(comm[1][1]);
  return OK;
}

result_t write_comm_request(dewlock_string_t *pw) {
  assert(pw && "pw must be non-null");
  assert(pw->buf && "pw->buf must be non-null");
  result_t result = ERR_COMM_WRITE;
  int fd = comm[0][1];

  size_t size = pw->len + 1;
  if (!write_full(fd, &size, sizeof(size))) {
    log_error("Failed to write pw size: %s", strerror(errno));
    goto out;
  }

  if (!write_full(fd, pw->buf, size)) {
    log_error("Failed to write pw buffer: %s", strerror(errno));
    goto out;
  }

  result = OK;

out:
  clear_password_buffer(pw);
  return result;
}

result_t read_comm_reply(bool *auth_success) {
  if (read_full(comm[1][0], auth_success, sizeof(*auth_success)) <= 0) {
    log_error("Failed to read pw result", NULL);
    return ERR_COMM_READ;
  }
  return OK;
}

int get_comm_reply_fd(void) { return comm[1][0]; }
