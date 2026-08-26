#include "background-image.h"
#include "cairo.h"
#include "cli.h"
#include "clock.h"
#include "comm.h"
#include "config.h"
#include "dewlock.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "log.h"
#include "loop.h"
#include "password-buffer.h"
#include "pool-buffer.h"
#include "seat.h"
#include "strcmp.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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

static struct dewlock_state g_state;

static const struct ext_session_lock_surface_v1_listener
    ext_session_lock_surface_v1_listener;

static void daemonize(void) {
  int fds[2];
  if (pipe(fds) != 0) {
    dewlock_log(LOG_ERROR, "Failed to pipe%s", "");
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
      dewlock_log(LOG_ERROR, "Failed to daemonize%s", "");
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
  cfg_t *cfg;
  cfg_get(&cfg);
  return (cfg->colors.background & 0xff) == 0xff;
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

  cfg_t *cfg;
  cfg_get(&cfg);
  if (surface_is_opaque(surface) && cfg->background.mode != BACKGROUND_MODE_CENTER &&
      cfg->background.mode != BACKGROUND_MODE_FIT) {
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
  (void)wl_output;
  (void)x;
  (void)y;
  (void)width_mm;
  (void)height_mm;
  (void)make;
  (void)model;
  (void)transform;
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
  (void)data;
  (void)output;
  (void)flags;
  (void)width;
  (void)height;
  (void)refresh;
  // Who cares
}

static void handle_wl_output_done(void *data, struct wl_output *output) {
  (void)output;
  struct dewlock_surface *surface = data;
  if (!surface->created && surface->state->run_display) {
    create_surface(surface);
  }
}

static void handle_wl_output_scale(void *data, struct wl_output *output,
                                   int32_t factor) {
  (void)output;
  struct dewlock_surface *surface = data;
  surface->scale = factor;
  if (surface->state->run_display) {
    surface->dirty = true;
    render(surface);
  }
}

static void handle_wl_output_name(void *data, struct wl_output *output,
                                  const char *name) {
  (void)output;
  struct dewlock_surface *surface = data;
  surface->output_name = strdup(name);
}

static void handle_wl_output_description(void *data, struct wl_output *output,
                                         const char *description) {
  (void)data;
  (void)output;
  (void)description;
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
  (void)lock;
  struct dewlock_state *state = data;
  state->locked = true;
}

static void
ext_session_lock_v1_handle_finished(void *data,
                                    struct ext_session_lock_v1 *lock) {
  (void)data;
  (void)lock;
  dewlock_log(LOG_ERROR, "Failed to lock session -- "
                         "is another lockscreen running?%s", "");
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
  (void)version;
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
    struct dewlock_seat *dewlock_seat = calloc(1, sizeof(struct dewlock_seat));
    dewlock_seat->state = state;
    wl_seat_add_listener(seat, &seat_listener, dewlock_seat);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    struct dewlock_surface *surface = calloc(1, sizeof(struct dewlock_surface));
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
  (void)registry;
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

void do_sigusr(int sig) {
  (void)sig;
  (void)write(sigusr_fds[1], "1", 1);
}

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

static void display_in(int fd, short mask, void *data) {
  (void)fd;
  (void)mask;
  (void)data;
  if (wl_display_dispatch(g_state.display) == -1) {
    g_state.run_display = false;
  }
}

static void comm_in(int fd, short mask, void *data) {
  (void)fd;
  (void)data;
  if (mask & POLLIN) {
    bool auth_success = false;
    if (!read_comm_reply(&auth_success)) {
      exit(EXIT_FAILURE);
    }
    if (auth_success) {
      // Authentication succeeded
      g_state.run_display = false;
    } else {
      g_state.auth_state = AUTH_STATE_INVALID;
      schedule_auth_idle(&g_state);
      ++g_state.failed_attempts;
      damage_state(&g_state);
    }
  } else if (mask & (POLLHUP | POLLERR)) {
    dewlock_log(LOG_ERROR, "Password checking subprocess crashed; exiting.%s", "");
    exit(EXIT_FAILURE);
  }
}

static void term_in(int fd, short mask, void *data) {
  (void)fd;
  (void)mask;
  (void)data;
  g_state.run_display = false;
}

int main(int argc, char **argv) {
  // Parse argv once, fully, before forking the password backend (and, for
  // the shadow backend, dropping setuid root) so the log level is settled
  // beforehand.
  cli_parse(argc, argv);
  cli_opts_t *opts;
  cli_get(&opts);
  dewlock_log_init(opts->debug ? LOG_DEBUG : LOG_ERROR);

  initialize_pw_backend(argc, argv);
  srand((unsigned int)time(NULL));

  g_state.failed_attempts = 0;
  wl_list_init(&g_state.images);

  g_state.username = getpwuid(getuid())->pw_name;

  char *resolved_config_path = NULL;
  const char *config_path = opts->config;
  if (!config_path) {
    resolved_config_path = cfg_path();
    config_path = resolved_config_path;
  }
  if (config_path) {
    dewlock_log(LOG_DEBUG, "Found config at %s", config_path);
  }
  cfg_read(config_path, &g_state);
  free(resolved_config_path);

  g_state.password.len = 0;
  g_state.password.cap = 1024;
  g_state.password.buf = password_buffer_create(g_state.password.cap);
  if (!g_state.password.buf) {
    return EXIT_FAILURE;
  }
  g_state.password.buf[0] = 0;

  state_set_time(&g_state);

  if (pipe(sigusr_fds) != 0) {
    dewlock_log(LOG_ERROR, "Failed to pipe%s", "");
    return EXIT_FAILURE;
  }
  if (fcntl(sigusr_fds[1], F_SETFL, O_NONBLOCK) == -1) {
    dewlock_log(LOG_ERROR, "Failed to make pipe end nonblocking%s", "");
    return EXIT_FAILURE;
  }

  wl_list_init(&g_state.surfaces);
  g_state.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  g_state.display = wl_display_connect(NULL);
  if (!g_state.display) {
    dewlock_log(LOG_ERROR, "Unable to connect to the compositor. "
                           "If your compositor is running, check or set the "
                           "WAYLAND_DISPLAY environment variable.%s", "");
    return EXIT_FAILURE;
  }
  g_state.eventloop = loop_create();

  struct wl_registry *registry = wl_display_get_registry(g_state.display);
  wl_registry_add_listener(registry, &registry_listener, &g_state);
  if (wl_display_roundtrip(g_state.display) == -1) {
    dewlock_log(LOG_ERROR, "wl_display_roundtrip() failed%s", "");
    return EXIT_FAILURE;
  }

  if (!g_state.compositor) {
    dewlock_log(LOG_ERROR, "Missing wl_compositor%s", "");
    return 1;
  }

  if (!g_state.subcompositor) {
    dewlock_log(LOG_ERROR, "Missing wl_subcompositor%s", "");
    return 1;
  }

  if (!g_state.shm) {
    dewlock_log(LOG_ERROR, "Missing wl_shm%s", "");
    return 1;
  }

  if (!g_state.ext_session_lock_manager_v1) {
    dewlock_log(LOG_ERROR, "Missing ext-session-lock-v1%s", "");
    return 1;
  }

  g_state.ext_session_lock_v1 =
      ext_session_lock_manager_v1_lock(g_state.ext_session_lock_manager_v1);
  ext_session_lock_v1_add_listener(g_state.ext_session_lock_v1,
                                   &ext_session_lock_v1_listener, &g_state);

  if (wl_display_roundtrip(g_state.display) == -1) {
    return 1;
  }

  struct dewlock_surface *surface;
  wl_list_for_each(surface, &g_state.surfaces, link) { create_surface(surface); }

  while (!g_state.locked) {
    if (wl_display_dispatch(g_state.display) < 0) {
      dewlock_log(LOG_ERROR, "wl_display_dispatch() failed%s", "");
      return 2;
    }
  }

  if (opts->ready_fd >= 0) {
    if (write(opts->ready_fd, "\n", 1) != 1) {
      dewlock_log(LOG_ERROR, "Failed to send readiness notification%s", "");
      return 2;
    }
    close(opts->ready_fd);
    opts->ready_fd = -1;
  }
  if (opts->daemonize) {
    daemonize();
  }

  loop_add_fd(g_state.eventloop, wl_display_get_fd(g_state.display), POLLIN,
              display_in, NULL);

  loop_add_fd(g_state.eventloop, get_comm_reply_fd(), POLLIN, comm_in, NULL);

  loop_add_fd(g_state.eventloop, sigusr_fds[0], POLLIN, term_in, NULL);

  schedule_clock_timer(&g_state);

  struct sigaction sa;
  sa.sa_handler = do_sigusr;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  sigaction(SIGUSR1, &sa, NULL);

  g_state.run_display = true;
  while (g_state.run_display) {
    errno = 0;
    if (wl_display_flush(g_state.display) == -1 && errno != EAGAIN) {
      break;
    }
    loop_poll(g_state.eventloop);
  }

  cancel_clock_timer(&g_state);
  ext_session_lock_v1_unlock_and_destroy(g_state.ext_session_lock_v1);
  wl_display_roundtrip(g_state.display);

  return 0;
}
