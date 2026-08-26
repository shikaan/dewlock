#include "config.h"
#include "background-image.h"
#include "color.h"
#include "log.h"
#include "strcmp.h"
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wordexp.h>

#ifndef SYSCONFDIR
#define SYSCONFDIR "/etc"
#endif

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

static bool file_exists(const char *path) {
  return path && access(path, R_OK) != -1;
}

// Most strings in this config can be distinguished by the first char
static inline bool streql(const char *a, const char *b) {
  return a[0] == b[0] && strcmp(a, b) == 0;
}

static cfg_t cfg = {0};

static void init(void) {
  cfg.font.family = "sans-serif";
  cfg.font.size = 16;
  cfg.background.mode = BACKGROUND_MODE_FILL;
  cfg.colors.background = 0xA3A3A3FF;
  cfg.colors.overlay = 0x00000055;
  cfg.colors.text = 0xFFFFFFFF;
  cfg.colors.warning = 0xffdd00ff;
  cfg.colors.error = 0xcc6566ff;
}

static char *join_args(char **argv, int argc) {
  assert(argc > 0 && "join_args requires at least one argument");
  size_t len = 0;
  int i;
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

// Loads the background image(s) referenced by cfg.background.path into
// state->images. A no-op if no path was set (no CLI flag sets this field,
// so it only ever comes from the config file).
static void load_image(struct dewlock_state *state) {
  char *raw_image = cfg.background.path;
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
        log_debug("Replacing image defined for output %s with %s",
                  image->output_name, image->path);
      } else {
        log_debug("Replacing default image with %s", image->path);
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
    image->path = join_args(p.we_wordv, (int)p.we_wordc);
    wordfree(&p);
  }

  // Load the actual image
  image->cairo_surface = load_background_image(image->path);
  if (!image->cairo_surface) {
    free(image);
    return;
  }
  wl_list_insert(&state->images, &image->link);
  log_debug("Loaded image %s for output %s", image->path,
            image->output_name ? image->output_name : "*");
}

char *cfg_path(void) {
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

void cfg_read(const char *path, struct dewlock_state *state) {
#define readstr(Prop, Value)                                                 \
  if (streql(key, Value)) {                                                  \
    (Prop) = strdup(value);                                                  \
    continue;                                                                \
  }
#define readcol(Prop, Value)                                                 \
  if (streql(key, Value)) {                                                  \
    (Prop) = color_from_string(value, Prop);                                 \
    continue;                                                                \
  }

  init();

  if (!path) {
    log_info("no configuration file, using defaults", NULL);
  } else {
    FILE *config_file = fopen(path, "r");
    if (!config_file) {
      log_warn("failed to load config at '%s', using defaults", path);
    } else {
      char *line = NULL;
      size_t line_size = 0;
      ssize_t nread;
      int line_number = 0;
      log_debug("config file:", NULL);
      while ((nread = getline(&line, &line_size, config_file)) != -1) {
        line_number++;

        if (line[nread - 1] == '\n') {
          line[--nread] = '\0';
        }

        log_debug("  %d | %s", line_number, line);
        if (!*line || line[0] == CONFIG_COMMENT) {
          continue;
        }

        char *separator = strchr(line, CONFIG_VALUE_SEPARATOR);
        if (!separator) {
          log_warn("invalid config line %d (missing '='), skipping",
                   line_number);
          continue;
        }

        *separator = '\0';
        char *value = separator + 1;

        char *dot = strchr(line, CONFIG_NAMESPACE_SEPARATOR);
        if (!dot) {
          log_warn("invalid config line %d (missing '.'), skipping",
                   line_number);
          continue;
        }
        *dot = '\0';
        char *key = dot + 1;
        char *namespace = line;

        if (streql(namespace, CONFIG_NAMESPACE_BACKGROUND)) {
          readstr(cfg.background.path, CONFIG_BACKGROUND_PATH);

          if (streql(key, CONFIG_BACKGROUND_MODE)) {
            cfg.background.mode = parse_background_mode(value);
            continue;
          }
        }

        if (streql(namespace, CONFIG_NAMESPACE_FONT)) {
          readstr(cfg.font.family, CONFIG_FONT_FAMILY);

          if (streql(key, CONFIG_FONT_SIZE)) {
            // FIXME: this feels unsafe
            cfg.font.size = (uint32_t)atol(value);
            continue;
          }
        }

        if (streql(namespace, CONFIG_NAMESPACE_COLOR)) {
          readcol(cfg.colors.background, CONFIG_COLOR_BACKGROUND);
          readcol(cfg.colors.overlay, CONFIG_COLOR_OVERLAY);
          readcol(cfg.colors.text, CONFIG_COLOR_TEXT);
          readcol(cfg.colors.warning, CONFIG_COLOR_WARNING);
          readcol(cfg.colors.error, CONFIG_COLOR_ERROR);
        }
      }

      free(line);
      fclose(config_file);
    }
  }

  load_image(state);
#undef readcol
#undef readstr
}

void cfg_get(cfg_t **out) { *out = &cfg; }
