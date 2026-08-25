#include "configuration.h"
#include "background-image.h"
#include "log.h"
#include <assert.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#define CONFIG_VALUE_SEPARATOR '='
#define CONFIG_NAMESPACE_SEPARATOR '.'
#define CONFIG_COMMENT '#'

#define CONFIG_NAMESPACE_BACKGROUND "background"
#define CONFIG_BACKGROUND_PATH "path"
#define CONFIG_BACKGROUND_MODE "mode"

#define CONFIG_NAMESPACE_FONT "font"
#define CONFIG_FONT_FAMILY "family"
#define CONFIG_FONT_SIZE "size"

#define CONFIG_NAMESPACE_COLOR "color"
#define CONFIG_COLOR_BACKGROUND "background"
#define CONFIG_COLOR_OVERLAY "overlay"
#define CONFIG_COLOR_TEXT "text"
#define CONFIG_COLOR_WARNING "warning"
#define CONFIG_COLOR_ERROR "error"

static uint32_t parse_color(const char *color) {
  if (color[0] == '#') {
    ++color;
  }

  int len = strlen(color);
  if (len != 6 && len != 8) {
    dewlock_log(LOG_DEBUG, "Invalid color %s, defaulting to white", color);
    return 0xFFFFFFFF;
  }
  uint32_t res = (uint32_t)strtoul(color, NULL, 16);
  if (strlen(color) == 6) {
    res = (res << 8) | 0xFF;
  }
  return res;
}

static char *join_args(char **argv, int argc) {
  assert(argc > 0);
  int len = 0, i;
  for (i = 0; i < argc; ++i) {
    len += strlen(argv[i]) + 1;
  }
  char *res = malloc(len);
  len = 0;
  for (i = 0; i < argc; ++i) {
    strcpy(res + len, argv[i]);
    len += strlen(argv[i]);
    res[len++] = ' ';
  }
  res[len - 1] = '\0';
  return res;
}

void load_image(struct dewlock_state *state) {
  char *raw_image = state->args.background.path;
  if (!raw_image)
    return;
  // [[<output>]:]<path>
  struct dewlock_image *image = calloc(1, sizeof(struct dewlock_image));
  char *separator = strchr(raw_image, ':');
  if (separator) {
    *separator = '\0';
    image->output_name = separator == raw_image ? NULL : strdup(raw_image);
    image->path = strdup(separator + 1);
  } else {
    image->output_name = NULL;
    image->path = strdup(raw_image);
  }

  struct dewlock_image *iter_image, *temp;
  wl_list_for_each_safe(iter_image, temp, &state->images, link) {
    if (lenient_strcmp(iter_image->output_name, image->output_name) == 0) {
      if (image->output_name) {
        dewlock_log(LOG_DEBUG, "Replacing image defined for output %s with %s",
                    image->output_name, image->path);
      } else {
        dewlock_log(LOG_DEBUG, "Replacing default image with %s", image->path);
      }
      wl_list_remove(&iter_image->link);
      free(iter_image->cairo_surface);
      free(iter_image->output_name);
      free(iter_image->path);
      free(iter_image);
      break;
    }
  }

  // The shell will not expand ~ to the value of $HOME when an output name is
  // given. Also, any image paths given in the config file need to have shell
  // expansions performed
  wordexp_t p;
  while (strstr(image->path, "  ")) {
    image->path = realloc(image->path, strlen(image->path) + 2);
    char *ptr = strstr(image->path, "  ") + 1;
    memmove(ptr + 1, ptr, strlen(ptr) + 1);
    *ptr = '\\';
  }
  if (wordexp(image->path, &p, 0) == 0) {
    free(image->path);
    image->path = join_args(p.we_wordv, p.we_wordc);
    wordfree(&p);
  }

  // Load the actual image
  image->cairo_surface = load_background_image(image->path);
  if (!image->cairo_surface) {
    free(image);
    return;
  }
  wl_list_insert(&state->images, &image->link);
  dewlock_log(LOG_DEBUG, "Loaded image %s for output %s", image->path,
              image->output_name ? image->output_name : "*");
}

int parse_cli_args(int argc, char **argv, struct dewlock_state *state,
                   char **config_path) {
  static struct option long_options[] = {
      {"config", required_argument, NULL, 'c'},
      {"debug", no_argument, NULL, 'd'},
      {"daemonize", no_argument, NULL, 'f'},
      {"ready-fd", required_argument, NULL, 'r'},
      {"help", no_argument, NULL, 'h'},
      {"version", no_argument, NULL, 'v'},
      {0, 0, 0, 0}};

  const char usage[] = "Usage: dewlock [options...]\n"
                       "\n"
                       "  -c, --config <config_file>       "
                       "Path to the config file.\n"
                       "  -d, --debug                      "
                       "Enable debugging output.\n"
                       "  -f, --daemonize                  "
                       "Detach from the controlling terminal after locking.\n"
                       "  -r, --ready-fd <fd>              "
                       "File descriptor to send readiness notifications to.\n"
                       "  -h, --help                       "
                       "Show help message and quit.\n"
                       "  -v, --version                    "
                       "Show the version number and quit.\n";

  int c;
  optind = 1;
  while (1) {
    int opt_idx = 0;
    c = getopt_long(argc, argv, "c:dfr:hv", long_options, &opt_idx);
    if (c == -1) {
      break;
    }
    switch (c) {
    case 'c':
      if (config_path) {
        *config_path = strdup(optarg);
      }
      break;
    case 'd':
      dewlock_log_init(LOG_DEBUG);
      break;
    case 'f':
      if (state) {
        state->args.daemonize = true;
      }
      break;
    case 'r':
      if (state) {
        state->args.ready_fd = strtol(optarg, NULL, 10);
      }
      break;
    case 'v':
      fprintf(stdout, "dewlock version " DEWLOCK_VERSION "\n");
      exit(EXIT_SUCCESS);
      break;
    default:
      fprintf(stderr, "%s", usage);
      return 1;
    }
  }

  return 0;
}

static bool file_exists(const char *path) {
  return path && access(path, R_OK) != -1;
}

char *get_config_path(void) {
  static const char *config_paths[] = {
      "$HOME/.dewlock/config",
      "$XDG_CONFIG_HOME/dewlock/config",
      SYSCONFDIR "/dewlock/config",
  };

  char *config_home = getenv("XDG_CONFIG_HOME");
  if (!config_home || config_home[0] == '\0') {
    config_paths[1] = "$HOME/.config/dewlock/config";
  }

  wordexp_t p;
  char *path;
  for (size_t i = 0; i < sizeof(config_paths) / sizeof(char *); ++i) {
    if (wordexp(config_paths[i], &p, 0) == 0) {
      path = strdup(p.we_wordv[0]);
      wordfree(&p);
      if (file_exists(path)) {
        return path;
      }
      free(path);
    }
  }

  return NULL;
}

int load_config(char *path, struct dewlock_state *state) {
  FILE *config = fopen(path, "r");
  if (!config) {
    dewlock_log(LOG_ERROR, "Failed to read config. Running without it.");
    return 0;
  }
  char *line = NULL;
  size_t line_size = 0;
  ssize_t nread;
  int line_number = 0;
  while ((nread = getline(&line, &line_size, config)) != -1) {
    line_number++;

    if (line[nread - 1] == '\n') {
      line[--nread] = '\0';
    }

    if (!*line || line[0] == CONFIG_COMMENT) {
      continue;
    }

    dewlock_log(LOG_DEBUG, "Config Line #%d: %s", line_number, line);
    char *separator = strchr(line, CONFIG_VALUE_SEPARATOR);
    if (!separator) {
      dewlock_log(LOG_ERROR, "Invalid config line. Skipping.");
      continue;
    }

    *separator = '\0';
    char *value = separator + 1;

    char *dot = strchr(line, CONFIG_NAMESPACE_SEPARATOR);
    if (!dot) {
      dewlock_log(LOG_ERROR, "Invalid config line. Skipping.");
      continue;
    }
    *dot = '\0';
    char *key = dot + 1;
    char *namespace = line;

    if (namespace[0] == CONFIG_NAMESPACE_BACKGROUND[0] &&
        !strcmp(namespace, CONFIG_NAMESPACE_BACKGROUND)) {
      if (!strcmp(key, CONFIG_BACKGROUND_PATH)) {
        state->args.background.path = strdup(value);
        continue;
      }

      if (!strcmp(key, CONFIG_BACKGROUND_MODE)) {
        state->args.background.mode = parse_background_mode(value);
        continue;
      }
    }

    if (namespace[0] == CONFIG_NAMESPACE_FONT[0] &&
        !strcmp(namespace, CONFIG_NAMESPACE_FONT)) {
      if (!strcmp(key, CONFIG_FONT_FAMILY)) {
        state->args.font.family = strdup(value);
        continue;
      }

      if (!strcmp(key, CONFIG_FONT_SIZE)) {
        state->args.font.size = atoi(value);
        continue;
      }
    }

    if (namespace[0] == CONFIG_NAMESPACE_COLOR[0] &&
        !strcmp(namespace, CONFIG_NAMESPACE_COLOR)) {
      if (!strcmp(key, CONFIG_COLOR_BACKGROUND)) {
        state->args.colors.background = parse_color(value);
        continue;
      }

      if (!strcmp(key, CONFIG_COLOR_OVERLAY)) {
        state->args.colors.overlay = parse_color(value);
        continue;
      }

      if (!strcmp(key, CONFIG_COLOR_TEXT)) {
        state->args.colors.text = parse_color(value);
        continue;
      }

      if (!strcmp(key, CONFIG_COLOR_WARNING)) {
        state->args.colors.warning = parse_color(value);
        continue;
      }

      if (!strcmp(key, CONFIG_COLOR_ERROR)) {
        state->args.colors.error = parse_color(value);
        continue;
      }
    }
  }
  free(line);
  fclose(config);
  return 0;
}
