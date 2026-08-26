#pragma once
#include <stddef.h>
#include <string.h>

// Like strcmp, but treats NULL as distinct from (and less than) any string.
static inline int lenient_strcmp(char *a, char *b) {
  if (a == b) {
    return 0;
  } else if (!a) {
    return -1;
  } else if (!b) {
    return 1;
  } else {
    return strcmp(a, b);
  }
}
