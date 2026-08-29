#include "safebuf.h"
#include "log.h"
#include "result.h"
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
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
        log_error("mlock() supported but failed too often", NULL);
        return ERR_SBUF_LOCK;
      }
      break;
    case EPERM:
      log_error("unable to mlock() password memory: unsupported: %s",
                strerror(errno));
      mlock_supported = false;
      return OK;
    default:
      log_error("unable to mlock() password memory: %s", strerror(errno));
      return ERR_SBUF_LOCK;
    }
  }

  return OK;
}

result_t sbuf_create(sbuf_t *sbuf, size_t cap) {
  assert(sbuf && "sbuf must be non-null");
  assert(cap > 0 && "cap must be positive");
  void *buf;
  int err = posix_memalign(&buf, (size_t)get_page_size(), cap);
  if (err) {
    // posix_memalign doesn't set errno according to the man page
    errno = err;
    log_error("failed to alloc password buffer: %s", strerror(errno));
    return ERR_SBUF_ALLOC;
  }

  result_t res = lock_buffer(buf, cap);
  if (res != OK) {
    free(buf);
    return res;
  }

  memset(buf, 0, cap);
  sbuf->buf = buf;
  sbuf->cap = cap;
  sbuf->len = 0;
  return OK;
}

void sbuf_destroy(sbuf_t *sbuf) {
  assert(sbuf && "sbuf must be non-null");
  assert(sbuf->buf && "sbuf->buf must be non-null");
  assert((uintptr_t)sbuf->buf % (uintptr_t)get_page_size() == 0 &&
         "sbuf->buf must be page-aligned");
  memset(sbuf->buf, 0, sbuf->cap);
  if (mlock_supported && munlock(sbuf->buf, sbuf->cap) != 0) {
    log_warn("unable to munlock() password memory: %s", strerror(errno));
  }
  free(sbuf->buf);
  sbuf->buf = NULL;
  sbuf->cap = 0;
  sbuf->len = 0;
}

void sbuf_clear(sbuf_t *sbuf) {
  assert(sbuf && "sbuf must be non-null");
  memset(sbuf->buf, 0, sbuf->cap);
  sbuf->len = 0;
}
