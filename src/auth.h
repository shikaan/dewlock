#pragma once

#include "result.h"
#include "safebuf.h"
#include <stdbool.h>

void auth_init(int argc, char **argv);
// Implemented by the selected backend (auth-pam.c or auth-shadow.c); runs in
// the child forked by auth_spawn_child() and never returns.
void auth_run(void);

result_t auth_spawn_child(void);
// Returns ERR_AUTH_EOF once the parent has closed its end of the pipe.
result_t auth_read_request(sbuf_t *pw);
result_t auth_write_reply(bool success);
// Requests the provided password to be checked. The password is always cleared
// when the function returns.
result_t auth_write_request(sbuf_t *pw);
result_t auth_read_reply(bool *auth_success);
// FD to poll for password authentication replies.
int auth_get_reply_fd(void);
