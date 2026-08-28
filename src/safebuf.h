#pragma once

#include "result.h"
#include <stddef.h>

typedef struct {
  char *buf;
  size_t cap;
  size_t len;
} sbuf_t;

result_t sbuf_create(sbuf_t *sbuf, size_t cap);
void sbuf_destroy(sbuf_t *sbuf);
// Wipes buf up to cap and resets len to 0; the allocation itself is kept.
void sbuf_clear(sbuf_t *sbuf);
