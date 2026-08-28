#pragma once

#include "result.h"
#include <stddef.h>

result_t password_buffer_create(size_t size, char **buffer);
void password_buffer_destroy(char *buffer, size_t size);
// FIXME: declared but never defined anywhere; src/auth-shadow.c calls this
// and fails to link (PAM=0 build only). Was removed at some point in history
// (see `git log -S"clear_buffer"`) without updating callers.
void clear_buffer(char *buf, size_t size);
