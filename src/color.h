#pragma once

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t color_t;

static inline color_t color_from_string(const char *str, color_t fallback) {
  size_t len = strlen(str);
  if (len != 6 && len != 8) {
    return fallback;
  }

  errno = 0;
  uint32_t res = (uint32_t)strtoul(str, NULL, 16);
  if (errno != 0) {
    return fallback;
  }

  return (color_t)res;
}
