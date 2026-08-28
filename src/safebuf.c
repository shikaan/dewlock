#include "safebuf.h"
#include "dewlock.h"
#include "log.h"
#include "result.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static bool mlock_supported = true;
static long int page_size = 0;

static long int get_page_size(void) {
  if (!page_size) {
    page_size = sysconf(_SC_PAGESIZE);
  }
  return page_size;
}

// lock_buffer expects addr to be page alligned
static result_t lock_buffer(char *addr, size_t size) {
  assert((uintptr_t)addr % (uintptr_t)get_page_size() == 0 &&
         "addr must be page-aligned");
  int retries = 5;
  while (mlock(addr, size) != 0 && retries > 0) {
    switch (errno) {
    case EAGAIN:
      retries--;
      if (retries == 0) {
        log_error("mlock() supported but failed too often.", NULL);
        return ERR_SBUF_LOCK;
      }
      break;
    case EPERM:
      log_error("Unable to mlock() password memory: Unsupported!: %s",
                strerror(errno));
      mlock_supported = false;
      return OK;
    default:
      log_error("Unable to mlock() password memory: %s", strerror(errno));
      return ERR_SBUF_LOCK;
    }
  }

  return OK;
}

result_t sbuf_create(size_t size, char **buffer) {
  assert(size > 0 && "size must be positive");
  void *buf;
  int err = posix_memalign(&buf, (size_t)get_page_size(), size);
  if (err) {
    // posix_memalign doesn't set errno according to the man page
    errno = err;
    log_error("failed to alloc password buffer: %s", strerror(errno));
    return ERR_SBUF_ALLOC;
  }

  result_t res = lock_buffer(buf, size);
  if (res != OK) {
    free(buf);
    return res;
  }

  *buffer = buf;
  return OK;
}

void sbuf_destroy(char *buffer, size_t size) {
  assert(buffer && "buffer must be non-null");
  assert((uintptr_t)buffer % (uintptr_t)get_page_size() == 0 &&
         "buffer must be page-aligned");
  memset(buffer, 0, size);
  if (mlock_supported && munlock(buffer, size) != 0) {
    log_warn("Unable to munlock() password memory: %s", strerror(errno));
  }
  free(buffer);
}

void sbuf_clear_string(dewlock_string_t *pw) {
  memset(pw->buf, 0, pw->cap);
  pw->len = 0;
}

void sbuf_clear(char *buf, size_t size) {
  assert(buf && "buf must be non-null");
  memset(buf, 0, size);
}
