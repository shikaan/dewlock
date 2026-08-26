#pragma once
#include <stdbool.h>

#define NAME "dewlock"

typedef struct {
  const char *config;
  bool debug;
  bool daemonize;
  int ready_fd;
} cli_opts_t;

void cli_parse(int argc, char *const *argv);
void cli_get(cli_opts_t **opts);
