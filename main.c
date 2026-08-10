#include "background-image.h"
#include "cairo.h"
#include "clock.h"
#include "comm.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "log.h"
#include "loop.h"
#include "password-buffer.h"
#include "pool-buffer.h"
#include "seat.h"
#include "dewlock.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
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

// This intentionally leaks memory (in the asan sense).
// Its life is bound the the applications, no point in freeing it.
static struct dewlock_state state;

static const struct ext_session_lock_surface_v1_listener
    ext_session_lock_surface_v1_listener;

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

int lenient_strcmp(char *a, char *b) {
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

static void daemonize(void) {
  int fds[2];
  if (pipe(fds) != 0) {
    dewlock_log(LOG_ERROR, "Failed to pipe");
    exit(1);
  }
  if (fork() == 0) {
    setsid();
    close(fds[0]);
    int devnull = open("/dev/null", O_RDWR);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(devnull);
    uint8_t success = 0;
    if (chdir("/") != 0) {
      write(fds[1], &success, 1);
      exit(1);
    }
    success = 1;
    if (write(fds[1], &success, 1) != 1) {
      exit(1);
    }
    close(fds[1]);
  } else {
    close(fds[1]);
    uint8_t success;
    if (read(fds[0], &success, 1) != 1 || !success) {
      dewlock_log(LOG_ERROR, "Failed to daemonize");
      exit(1);
    }
    close(fds[0]);
    exit(0);
  }
}

static void destroy_surface(struct dewlock_surface *surface) {
  if (surface->frame != NULL) {
    wl_callback_destroy(surface->frame);
  }
  wl_list_remove(&surface->link);
  if (surface->ext_session_lock_surface_v1 != NULL) {
    ext_session_lock_surface_v1_destroy(surface->ext_session_lock_surface_v1);
  }
  if (surface->subsurface) {
    wl_subsurface_destroy(surface->subsurface);
  }
  if (surface->child) {
    wl_surface_destroy(surface->child);
  }
  if (surface->surface != NULL) {
    wl_surface_destroy(surface->surface);
  }
  destroy_buffer(&surface->indicator_buffers[0]);
  destroy_buffer(&surface->indicator_buffers[1]);
  wl_output_release(surface->output);
  free(surface);
}

static cairo_surface_t *select_image(struct dewlock_state *state,
                                     struct dewlock_surface *surface);

static bool surface_is_opaque(struct dewlock_surface *surface) {
  if (surface->image) {
    return cairo_surface_get_content(surface->image) == CAIRO_CONTENT_COLOR;
  }
  return (surface->state->args.colors.background & 0xff) == 0xff;
}

static void create_surface(struct dewlock_surface *surface) {
  struct dewlock_state *state = surface->state;

  surface->image = select_image(state, surface);

  surface->surface = wl_compositor_create_surface(state->compositor);
  assert(surface->surface);

  surface->child = wl_compositor_create_surface(state->compositor);
  assert(surface->child);
  surface->subsurface = wl_subcompositor_get_subsurface(
      state->subcompositor, surface->child, surface->surface);
  assert(surface->subsurface);
  wl_subsurface_set_sync(surface->subsurface);

  surface->ext_session_lock_surface_v1 = ext_session_lock_v1_get_lock_surface(
      state->ext_session_lock_v1, surface->surface, surface->output);
  ext_session_lock_surface_v1_add_listener(
      surface->ext_session_lock_surface_v1,
      &ext_session_lock_surface_v1_listener, surface);

  if (surface_is_opaque(surface) &&
      surface->state->args.background.mode != BACKGROUND_MODE_CENTER &&
      surface->state->args.background.mode != BACKGROUND_MODE_FIT) {
    struct wl_region *region =
        wl_compositor_create_region(surface->state->compositor);
    wl_region_add(region, 0, 0, INT32_MAX, INT32_MAX);
    wl_surface_set_opaque_region(surface->surface, region);
    wl_region_destroy(region);
  }

  surface->created = true;
}

static void ext_session_lock_surface_v1_handle_configure(
    void *data, struct ext_session_lock_surface_v1 *lock_surface,
    uint32_t serial, uint32_t width, uint32_t height) {
  struct dewlock_surface *surface = data;
  surface->width = width;
  surface->height = height;
  ext_session_lock_surface_v1_ack_configure(lock_surface, serial);
  surface->dirty = true;
  render(surface);
}

static const struct ext_session_lock_surface_v1_listener
    ext_session_lock_surface_v1_listener = {
        .configure = ext_session_lock_surface_v1_handle_configure,
};

void damage_state(struct dewlock_state *state) {
  struct dewlock_surface *surface;
  wl_list_for_each(surface, &state->surfaces, link) {
    surface->dirty = true;
    render(surface);
  }
}

static void handle_wl_output_geometry(void *data, struct wl_output *wl_output,
                                      int32_t x, int32_t y, int32_t width_mm,
                                      int32_t height_mm, int32_t subpixel,
                                      const char *make, const char *model,
                                      int32_t transform) {
  struct dewlock_surface *surface = data;
  surface->subpixel = subpixel;
  if (surface->state->run_display) {
    surface->dirty = true;
    render(surface);
  }
}

static void handle_wl_output_mode(void *data, struct wl_output *output,
                                  uint32_t flags, int32_t width, int32_t height,
                                  int32_t refresh) {
  // Who cares
}

static void handle_wl_output_done(void *data, struct wl_output *output) {
  struct dewlock_surface *surface = data;
  if (!surface->created && surface->state->run_display) {
    create_surface(surface);
  }
}

static void handle_wl_output_scale(void *data, struct wl_output *output,
                                   int32_t factor) {
  struct dewlock_surface *surface = data;
  surface->scale = factor;
  if (surface->state->run_display) {
    surface->dirty = true;
    render(surface);
  }
}

static void handle_wl_output_name(void *data, struct wl_output *output,
                                  const char *name) {
  struct dewlock_surface *surface = data;
  surface->output_name = strdup(name);
}

static void handle_wl_output_description(void *data, struct wl_output *output,
                                         const char *description) {
  // Who cares
}

struct wl_output_listener _wl_output_listener = {
    .geometry = handle_wl_output_geometry,
    .mode = handle_wl_output_mode,
    .done = handle_wl_output_done,
    .scale = handle_wl_output_scale,
    .name = handle_wl_output_name,
    .description = handle_wl_output_description,
};

static void
ext_session_lock_v1_handle_locked(void *data,
                                  struct ext_session_lock_v1 *lock) {
  struct dewlock_state *state = data;
  state->locked = true;
}

static void
ext_session_lock_v1_handle_finished(void *data,
                                    struct ext_session_lock_v1 *lock) {
  dewlock_log(LOG_ERROR, "Failed to lock session -- "
                          "is another lockscreen running?");
  exit(2);
}

static const struct ext_session_lock_v1_listener ext_session_lock_v1_listener =
    {
        .locked = ext_session_lock_v1_handle_locked,
        .finished = ext_session_lock_v1_handle_finished,
};

static void handle_global(void *data, struct wl_registry *registry,
                          uint32_t name, const char *interface,
                          uint32_t version) {
  struct dewlock_state *state = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    state->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
    state->subcompositor =
        wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    struct wl_seat *seat =
        wl_registry_bind(registry, name, &wl_seat_interface, 4);
    struct dewlock_seat *dewlock_seat =
        calloc(1, sizeof(struct dewlock_seat));
    dewlock_seat->state = state;
    wl_seat_add_listener(seat, &seat_listener, dewlock_seat);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct dewlock_surface *surface =
        calloc(1, sizeof(struct dewlock_surface));
    surface->state = state;
    surface->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
    surface->output_global_name = name;
    wl_output_add_listener(surface->output, &_wl_output_listener, surface);
    wl_list_insert(&state->surfaces, &surface->link);
  } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) ==
             0) {
    state->ext_session_lock_manager_v1 = wl_registry_bind(
        registry, name, &ext_session_lock_manager_v1_interface, 1);
  }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  struct dewlock_state *state = data;
  struct dewlock_surface *surface;
  wl_list_for_each(surface, &state->surfaces, link) {
    if (surface->output_global_name == name) {
      destroy_surface(surface);
      break;
    }
  }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove,
};

static int sigusr_fds[2] = {-1, -1};

void do_sigusr(int sig) { (void)write(sigusr_fds[1], "1", 1); }

static cairo_surface_t *select_image(struct dewlock_state *state,
                                     struct dewlock_surface *surface) {
  struct dewlock_image *image;
  cairo_surface_t *default_image = NULL;
  wl_list_for_each(image, &state->images, link) {
    if (lenient_strcmp(image->output_name, surface->output_name) == 0) {
      return image->cairo_surface;
    } else if (!image->output_name) {
      default_image = image->cairo_surface;
    }
  }
  return default_image;
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

static void load_image(struct dewlock_state *state) {
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

static int parse_cli_args(int argc, char **argv, struct dewlock_state *state,
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

static char *get_config_path(void) {
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

static int load_config(char *path, struct dewlock_state *state) {
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

static void display_in(int fd, short mask, void *data) {
  if (wl_display_dispatch(state.display) == -1) {
    state.run_display = false;
  }
}

static void comm_in(int fd, short mask, void *data) {
  if (mask & POLLIN) {
    bool auth_success = false;
    if (!read_comm_reply(&auth_success)) {
      exit(EXIT_FAILURE);
    }
    if (auth_success) {
      // Authentication succeeded
      state.run_display = false;
    } else {
      state.auth_state = AUTH_STATE_INVALID;
      schedule_auth_idle(&state);
      ++state.failed_attempts;
      damage_state(&state);
    }
  } else if (mask & (POLLHUP | POLLERR)) {
    dewlock_log(LOG_ERROR, "Password checking subprocess crashed; exiting.");
    exit(EXIT_FAILURE);
  }
}

static void term_in(int fd, short mask, void *data) {
  state.run_display = false;
}

// Check for --debug 'early' we also apply the correct loglevel
// to the forked child, without having to first proces all of the
// configuration (including from file) before forking and (in the
// case of the shadow backend) dropping privileges
void log_init(int argc, char **argv) {
  static struct option long_options[] = {{"debug", no_argument, NULL, 'd'},
                                         {0, 0, 0, 0}};
  int c;
  optind = 1;
  while (1) {
    int opt_idx = 0;
    c = getopt_long(argc, argv, "-:d", long_options, &opt_idx);
    if (c == -1) {
      break;
    }
    switch (c) {
    case 'd':
      dewlock_log_init(LOG_DEBUG);
      return;
    }
  }
  dewlock_log_init(LOG_ERROR);
}

int main(int argc, char **argv) {
  log_init(argc, argv);
  initialize_pw_backend(argc, argv);
  srand(time(NULL));

  state.failed_attempts = 0;
  state.args = (struct dewlock_args){
      .font.family = "sans-serif",
      .font.size = 16,
      .background.mode = BACKGROUND_MODE_FILL,
      .colors.background = 0xA3A3A3FF,
      .colors.overlay = 0x00000055,
      .colors.text = 0xFFFFFFFF,
      .colors.warning = 0xffdd00ff,
      .colors.error = 0xcc6566ff,
      .ready_fd = -1,
  };
  wl_list_init(&state.images);

  state.username = getpwuid(getuid())->pw_name;

  char *config_path = NULL;
  int result = parse_cli_args(argc, argv, NULL, &config_path);
  if (result != 0) {
    free(config_path);
    return result;
  }
  if (!config_path) {
    config_path = get_config_path();
  }

  if (config_path) {
    dewlock_log(LOG_DEBUG, "Found config at %s", config_path);
    int config_status = load_config(config_path, &state);
    free(config_path);
    if (config_status != 0) {
      return config_status;
    }
  }

  if (argc > 1) {
    dewlock_log(LOG_DEBUG, "Parsing CLI Args");
    int result = parse_cli_args(argc, argv, &state, NULL);
    if (result != 0) {
      return result;
    }
  }

  state.password.len = 0;
  state.password.cap = 1024;
  state.password.buf = password_buffer_create(state.password.cap);
  if (!state.password.buf) {
    return EXIT_FAILURE;
  }
  state.password.buf[0] = 0;

  state_set_time(&state);
  load_image(&state);

  if (pipe(sigusr_fds) != 0) {
    dewlock_log(LOG_ERROR, "Failed to pipe");
    return EXIT_FAILURE;
  }
  if (fcntl(sigusr_fds[1], F_SETFL, O_NONBLOCK) == -1) {
    dewlock_log(LOG_ERROR, "Failed to make pipe end nonblocking");
    return EXIT_FAILURE;
  }

  wl_list_init(&state.surfaces);
  state.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  state.display = wl_display_connect(NULL);
  if (!state.display) {
    dewlock_log(LOG_ERROR, "Unable to connect to the compositor. "
                            "If your compositor is running, check or set the "
                            "WAYLAND_DISPLAY environment variable.");
    return EXIT_FAILURE;
  }
  state.eventloop = loop_create();

  struct wl_registry *registry = wl_display_get_registry(state.display);
  wl_registry_add_listener(registry, &registry_listener, &state);
  if (wl_display_roundtrip(state.display) == -1) {
    dewlock_log(LOG_ERROR, "wl_display_roundtrip() failed");
    return EXIT_FAILURE;
  }

  if (!state.compositor) {
    dewlock_log(LOG_ERROR, "Missing wl_compositor");
    return 1;
  }

  if (!state.subcompositor) {
    dewlock_log(LOG_ERROR, "Missing wl_subcompositor");
    return 1;
  }

  if (!state.shm) {
    dewlock_log(LOG_ERROR, "Missing wl_shm");
    return 1;
  }

  if (!state.ext_session_lock_manager_v1) {
    dewlock_log(LOG_ERROR, "Missing ext-session-lock-v1");
    return 1;
  }

  state.ext_session_lock_v1 =
      ext_session_lock_manager_v1_lock(state.ext_session_lock_manager_v1);
  ext_session_lock_v1_add_listener(state.ext_session_lock_v1,
                                   &ext_session_lock_v1_listener, &state);

  if (wl_display_roundtrip(state.display) == -1) {
    return 1;
  }

  struct dewlock_surface *surface;
  wl_list_for_each(surface, &state.surfaces, link) { create_surface(surface); }

  while (!state.locked) {
    if (wl_display_dispatch(state.display) < 0) {
      dewlock_log(LOG_ERROR, "wl_display_dispatch() failed");
      return 2;
    }
  }

  if (state.args.ready_fd >= 0) {
    if (write(state.args.ready_fd, "\n", 1) != 1) {
      dewlock_log(LOG_ERROR, "Failed to send readiness notification");
      return 2;
    }
    close(state.args.ready_fd);
    state.args.ready_fd = -1;
  }
  if (state.args.daemonize) {
    daemonize();
  }

  loop_add_fd(state.eventloop, wl_display_get_fd(state.display), POLLIN,
              display_in, NULL);

  loop_add_fd(state.eventloop, get_comm_reply_fd(), POLLIN, comm_in, NULL);

  loop_add_fd(state.eventloop, sigusr_fds[0], POLLIN, term_in, NULL);

  schedule_clock_timer(&state);

  struct sigaction sa;
  sa.sa_handler = do_sigusr;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa, NULL);

  state.run_display = true;
  while (state.run_display) {
    errno = 0;
    if (wl_display_flush(state.display) == -1 && errno != EAGAIN) {
      break;
    }
    loop_poll(state.eventloop);
  }

  cancel_clock_timer(&state);
  ext_session_lock_v1_unlock_and_destroy(state.ext_session_lock_v1);
  wl_display_roundtrip(state.display);

  return 0;
}
