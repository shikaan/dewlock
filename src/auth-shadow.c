#undef _POSIX_C_SOURCE
#define _XOPEN_SOURCE // for crypt
#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __GLIBC__
// GNU, you damn slimy bastard
#include <crypt.h>
#endif
#include "auth.h"
#include "log.h"
#include "password-buffer.h"
#include "result.h"

char *encpw = NULL;

void auth_init(int argc, char **argv) {
  (void)argc;
  (void)argv;
  /* This code runs as root */
  struct passwd *pwent = getpwuid(getuid());
  if (!pwent) {
    log_error("failed to getpwuid: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  encpw = pwent->pw_passwd;
  if (strcmp(encpw, "x") == 0) {
    struct spwd *swent = getspnam(pwent->pw_name);
    if (!swent) {
      log_error("failed to getspnam: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }
    encpw = swent->sp_pwdp;
  }

  if (setgid(getgid()) != 0) {
    log_error("Unable to drop root: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (setuid(getuid()) != 0) {
    log_error("Unable to drop root: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  if (setuid(0) != -1 || setgid(0) != -1) {
    log_error("Unable to drop root (we shouldn't be "
              "able to restore it after setuid/setgid): %s",
              strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* This code does not run as root */
  log_debug("Prepared to authorize user %s", pwent->pw_name);

  if (auth_spawn_child() != OK) {
    exit(EXIT_FAILURE);
  }

  /* Buffer is only used by the child */
  clear_buffer(encpw, strlen(encpw));
  encpw = NULL;
}

void auth_run(void) {
  assert(encpw != NULL && "encpw must be set before forking the pw backend child");
  while (1) {
    char *buf;
    size_t size;
    result_t res = auth_read_request(&buf, &size);
    if (res == ERR_AUTH_EOF) {
      break;
    } else if (res != OK) {
      exit(EXIT_FAILURE);
    }

    const char *c = crypt(buf, encpw);
    password_buffer_destroy(buf, size);
    buf = NULL;

    if (c == NULL) {
      log_error("crypt failed: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }
    bool success = strcmp(c, encpw) == 0;

    if (auth_write_reply(success) != OK) {
      exit(EXIT_FAILURE);
    }

    sleep(2);
  }

  clear_buffer(encpw, strlen(encpw));
  exit(EXIT_SUCCESS);
}
