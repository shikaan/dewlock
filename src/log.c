#include "log.h"
#include <stdarg.h>
#include <stdio.h>

static log_level_t current_level = LOG_LEVEL_ERROR;
static const char *LOG_LEVEL[LOG_LEVELS] = {
    "NONE", "ERROR", "WARN", "INFO", "DEBUG",
};

void log_init(log_level_t level) { current_level = level; }

void _log_put(log_level_t level, const char *file, int line, const char *fmt,
              ...) {
  if (level > current_level)
    return;

  fprintf(stderr, "[%s:%d] %s - ", file, line, LOG_LEVEL[level]);

  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
}
