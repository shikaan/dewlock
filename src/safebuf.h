#pragma once

#include "result.h"
#include <stddef.h>

typedef struct dewlock_string dewlock_string_t;

result_t sbuf_create(size_t size, char **buffer);
void sbuf_destroy(char *buffer, size_t size);
void sbuf_clear_string(dewlock_string_t *pw);
// Wipes buf in place without freeing it, for sensitive memory not owned by
// sbuf_create (e.g. a hash string handed back by getspnam()).
void sbuf_clear(char *buf, size_t size);
