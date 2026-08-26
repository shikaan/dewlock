#pragma once

typedef enum {
  LOG_LEVEL_NONE,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_WARN,
  LOG_LEVEL_INFO,
  LOG_LEVEL_DEBUG,

  LOG_LEVELS
} log_level_t;

void log_init(log_level_t level);

void _log_put(log_level_t level, const char *file, int line, const char* fmt, ...);
#define log_put(Level, Fmt, ...) \
  _log_put(Level, __FILE__, __LINE__, Fmt, ##__VA_ARGS__);

#define log_error(Fmt, ...) log_put(LOG_LEVEL_ERROR, Fmt, ##__VA_ARGS__)
#define log_warn(Fmt, ...) log_put(LOG_LEVEL_WARN, Fmt, ##__VA_ARGS__)
#define log_info(Fmt, ...) log_put(LOG_LEVEL_INFO, Fmt, ##__VA_ARGS__)
#define log_debug(Fmt, ...) log_put(LOG_LEVEL_DEBUG, Fmt, ##__VA_ARGS__)
