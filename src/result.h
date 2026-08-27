#pragma once

typedef enum {
  OK,
  ERROR,

  ERR_CTX,
  ERR_CTX_ALLOCATION,
  ERR_CTX_CAIRO,
  ERR_CTX_NO_BUFFERS,

  RESULTS
} result_t;
