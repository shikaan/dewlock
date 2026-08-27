#pragma once

#include "result.h"
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

typedef struct dewlock_string dewlock_string_t;

result_t spawn_comm_child(void);
// Returns ERR_COMM_EOF once the parent has closed its end of the pipe.
result_t read_comm_request(char **buf_ptr, size_t *size);
result_t write_comm_reply(bool success);
// Requests the provided password to be checked. The password is always cleared
// when the function returns.
result_t write_comm_request(dewlock_string_t *pw);
result_t read_comm_reply(bool *auth_success);
// FD to poll for password authentication replies.
int get_comm_reply_fd(void);
