#pragma once

#include <stddef.h>

char *password_buffer_create(size_t size);
void password_buffer_destroy(char *buffer, size_t size);
