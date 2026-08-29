#include "cli.h"
#include <assert.h>
#include <getopt.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define len(Array) sizeof(Array) / sizeof(Array[0])

static const char *OPTSTR = ":c:dfr:hv";
static struct option OPTIONS[] = {
    {"config", required_argument, 0, 'c'},
    {"debug", no_argument, 0, 'd'},
    {"daemonize", no_argument, 0, 'f'},
    {"ready-fd", required_argument, 0, 'r'},
    {"help", no_argument, 0, 'h'},
    {"version", no_argument, 0, 'v'},
    {0, 0, 0, 0},
};
#define NUM_OPTIONS (len(OPTIONS) - 1)
static const char *DESC[NUM_OPTIONS] = {
    "Path to the config file.",
    "Enable debugging output.",
    "Detach from the controlling terminal after locking.",
    "File descriptor to send readiness notifications to.",
    "Show this help message and quit.",
    "Show the version number and quit.",
};
static const char *ARGS[NUM_OPTIONS] = {
    "path", "", "", "fd", "", "",
};

static cli_opts_t cli_opts = {
    .config = NULL,
    .debug = false,
    .daemonize = false,
    .ready_fd = -1,
};

static void print_help(void) {
  const char *fmt = "  -%c, --%-27s%s\n";
  FILE *out = stderr;

  fprintf(out, "Usage: %s [options...]\n\n", NAME);

  for (size_t i = 0; i < NUM_OPTIONS; i++) {
    struct option opt = OPTIONS[i];

    if (opt.has_arg == no_argument) {
      fprintf(out, fmt, opt.val, opt.name, DESC[i]);
      continue;
    }

    char name_args[32];
    snprintf(name_args, sizeof(name_args), "%s <%s>", opt.name, ARGS[i]);
    fprintf(out, fmt, opt.val, name_args, DESC[i]);
  }
}

static void print_version(void) {
  FILE *out = stdout;
  // VERSION and SHA come from CFLAGS as -DVERSION -DSHA
  fprintf(out, "%s version %s (%s)\n", NAME, VERSION, SHA);
}

static void print_error(int opt, char *const *argv) {
  FILE *out = stdout;
  if (opt == '?') {
    if (optopt != 0) {
      fprintf(out, "%s: invalid option '-%c'\n", NAME, optopt);
    } else {
      fprintf(out, "%s: invalid option '%s'\n", NAME, argv[optind - 1]);
    }
  } else {
    fprintf(out, "%s: option '%s' requires an argument\n", NAME, argv[optind - 1]);
  }
}

result_t cli_parse(int argc, char *const *argv) {
  assert(argc > 0 && "argc must be positive");
  assert(argv && "argv must be non-null");
  int longind;

  int opt;
  while ((opt = getopt_long(argc, argv, OPTSTR, OPTIONS, &longind)) != -1) {
    switch (opt) {
    case 'c':
      cli_opts.config = optarg;
      break;
    case 'd':
      cli_opts.debug = true;
      break;
    case 'f':
      cli_opts.daemonize = true;
      break;
    case 'r':
      cli_opts.ready_fd = (int)strtol(optarg, NULL, 10);
      break;
    case 'h':
      print_help();
      exit(0);
    case 'v':
      print_version();
      exit(0);
    default:
      print_error(opt, argv);
      print_help();
      return ERR_CLI_INVALID_OPTION;
    }
  }

  return OK;
}

void cli_get(cli_opts_t **opts) {
  assert(opts && "opts must be non-null");
  *opts = &cli_opts;
}

#undef len
