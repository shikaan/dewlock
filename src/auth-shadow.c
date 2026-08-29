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
#include "result.h"
#include "safebuf.h"

static sbuf_t hashed_password = {0};

void auth_init(int argc, char **argv) {
  (void)argc;
  (void)argv;
  /* This code runs as root */
  struct passwd *pwent = getpwuid(getuid());
  if (!pwent) {
    log_error("failed to getpwuid: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  hashed_password.buf = pwent->pw_passwd;
  if (strcmp(hashed_password.buf, "x") == 0) {
    struct spwd *entry = getspnam(pwent->pw_name);
    if (!entry) {
      log_error("cannot find shadow password entry, see 'man dewlock' for info "
                "on usage without PAM",
                NULL);
      exit(EXIT_FAILURE);
    }
    hashed_password.buf = entry->sp_pwdp;
  }
  hashed_password.cap = hashed_password.len = strlen(hashed_password.buf);

  if (setgid(getgid()) != 0 || setuid(getuid()) != 0 || setuid(0) != -1 ||
      setgid(0) != -1) {
    log_error("unable to drop root, aborting: %s", strerror(errno));
    exit(EXIT_FAILURE);
  }

  /* This code does not run as root */
  log_debug("prepared to authorize user %s", pwent->pw_name);

  if (auth_spawn_child() != OK) {
    exit(EXIT_FAILURE);
  }

  /* Buffer is only used by the child */
  sbuf_clear(&hashed_password);
  hashed_password.buf = NULL;
}

void auth_run(void) {
  assert(hashed_password.buf != NULL &&
         "encpw must be set before forking the pw backend child");
  while (1) {
    sbuf_t pw = {0};
    result_t res = auth_read_request(&pw);
    if (res == ERR_AUTH_EOF) {
      break;
    } else if (res != OK) {
      exit(EXIT_FAILURE);
    }

    const char *c = crypt(pw.buf, hashed_password.buf);
    sbuf_destroy(&pw);

    if (c == NULL) {
      log_error("crypt failed: %s", strerror(errno));
      exit(EXIT_FAILURE);
    }
    bool success = strcmp(c, hashed_password.buf) == 0;

    if (auth_write_reply(success) != OK) {
      exit(EXIT_FAILURE);
    }

    sleep(2);
  }

  sbuf_clear(&hashed_password);
  exit(EXIT_SUCCESS);
}
