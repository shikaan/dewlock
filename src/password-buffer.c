#include "password-buffer.h"
#include "dewlock.h"
#include "log.h"
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

// password_buffer_lock expects addr to be page alligned
static bool password_buffer_lock(char *addr, size_t size) {
  assert((uintptr_t)addr % (uintptr_t)get_page_size() == 0 &&
         "addr must be page-aligned");
  int retries = 5;
  while (mlock(addr, size) != 0 && retries > 0) {
    switch (errno) {
    case EAGAIN:
      retries--;
      if (retries == 0) {
        log_error("mlock() supported but failed too often.", NULL);
        return false;
      }
      break;
    case EPERM:
      log_error("Unable to mlock() password memory: Unsupported!: %s",
                strerror(errno));
      mlock_supported = false;
      return true;
    default:
      log_error("Unable to mlock() password memory: %s", strerror(errno));
      return false;
    }
  }

  return true;
}

// password_buffer_unlock expects addr to be page alligned
static bool password_buffer_unlock(char *addr, size_t size) {
  assert((uintptr_t)addr % (uintptr_t)get_page_size() == 0 &&
         "addr must be page-aligned");
  if (mlock_supported) {
    if (munlock(addr, size) != 0) {
      log_error("Unable to munlock() password memory: %s", strerror(errno));
      return false;
    }
  }

  return true;
}

char *password_buffer_create(size_t size) {
  assert(size > 0 && "size must be positive");
  void *buffer;
  int result = posix_memalign(&buffer, (size_t)get_page_size(), size);
  if (result) {
    // posix_memalign doesn't set errno according to the man page
    errno = result;
    log_error("failed to alloc password buffer: %s", strerror(errno));
    return NULL;
  }

  if (!password_buffer_lock(buffer, size)) {
    free(buffer);
    return NULL;
  }

  return buffer;
}

void password_buffer_destroy(char *buffer, size_t size) {
  assert(buffer && "buffer must be non-null");
  memset(buffer, 0, size);
  password_buffer_unlock(buffer, size);
  free(buffer);
}
