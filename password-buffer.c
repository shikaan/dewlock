#include "password-buffer.h"
#include "dewlock.h"
#include "log.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

static bool mlock_supported = true;
static long int page_size = 0;

static long int get_page_size() {
  if (!page_size) {
    page_size = sysconf(_SC_PAGESIZE);
  }
  return page_size;
}

// password_buffer_lock expects addr to be page alligned
static bool password_buffer_lock(char *addr, size_t size) {
  int retries = 5;
  while (mlock(addr, size) != 0 && retries > 0) {
    switch (errno) {
    case EAGAIN:
      retries--;
      if (retries == 0) {
        dewlock_log(LOG_ERROR, "mlock() supported but failed too often.");
        return false;
      }
      break;
    case EPERM:
      dewlock_log_errno(LOG_ERROR,
                        "Unable to mlock() password memory: Unsupported!");
      mlock_supported = false;
      return true;
    default:
      dewlock_log_errno(LOG_ERROR, "Unable to mlock() password memory.");
      return false;
    }
  }

  return true;
}

// password_buffer_unlock expects addr to be page alligned
static bool password_buffer_unlock(char *addr, size_t size) {
  if (mlock_supported) {
    if (munlock(addr, size) != 0) {
      dewlock_log_errno(LOG_ERROR, "Unable to munlock() password memory.");
      return false;
    }
  }

  return true;
}

char *password_buffer_create(size_t size) {
  void *buffer;
  int result = posix_memalign(&buffer, get_page_size(), size);
  if (result) {
    // posix_memalign doesn't set errno according to the man page
    errno = result;
    dewlock_log_errno(LOG_ERROR, "failed to alloc password buffer");
    return NULL;
  }

  if (!password_buffer_lock(buffer, size)) {
    free(buffer);
    return NULL;
  }

  return buffer;
}

void password_buffer_destroy(char *buffer, size_t size) {
  memset(buffer, 0, size);
  password_buffer_unlock(buffer, size);
  free(buffer);
}
