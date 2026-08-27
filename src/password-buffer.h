#pragma once

#include "result.h"
#include <stddef.h>

result_t password_buffer_create(size_t size, char **buffer);
void password_buffer_destroy(char *buffer, size_t size);
