#include "background-image.h"
#include "cairo.h"
#include "cli.h"
#include "clock.h"
#include "comm.h"
#include "config.h"
#include "ctx.h"
#include "dewlock.h"
#include "ext-session-lock-v1-client-protocol.h"
#include "log.h"
#include "loop.h"
#include "password-buffer.h"
#include "result.h"
#include "seat.h"
#include "strcmp.h"
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>

static dewlock_state_t state;

static const struct ext_session_lock_surface_v1_listener
    ext_session_lock_surface_v1_listener;

static void daemonize(void) {
  int fds[2];
  if (pipe(fds) != 0) {
    log_error("Failed to pipe", NULL);
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
      log_error("Failed to daemonize", NULL);
      exit(1);
    }
    close(fds[0]);
    exit(0);
  }
}

static void destroy_surface(dewlock_surface_t *surface) {
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
  ctx_deinit(surface->buffers);
  wl_output_release(surface->output);
  free(surface);
}

static cairo_surface_t *select_image(dewlock_state_t *state,
                                     dewlock_surface_t *surface);

static bool surface_is_opaque(dewlock_surface_t *surface) {
  if (surface->image) {
    return cairo_surface_get_content(surface->image) == CAIRO_CONTENT_COLOR;
  }
  cfg_t *cfg;
  cfg_get(&cfg);
  return (cfg->colors.background & 0xff) == 0xff;
}

static void create_surface(dewlock_surface_t *surface) {
  dewlock_state_t *s = surface->state;

  surface->image = select_image(s, surface);

  surface->surface = wl_compositor_create_surface(s->compositor);
  if (!surface->surface) {
    log_error("wl_compositor_create_surface failed", NULL);
    exit(EXIT_FAILURE);
  }

  surface->child = wl_compositor_create_surface(s->compositor);
  if (!surface->child) {
    log_error("wl_compositor_create_surface failed", NULL);
    exit(EXIT_FAILURE);
  }
  surface->subsurface = wl_subcompositor_get_subsurface(
      s->subcompositor, surface->child, surface->surface);
  if (!surface->subsurface) {
    log_error("wl_subcompositor_get_subsurface failed", NULL);
    exit(EXIT_FAILURE);
  }
  wl_subsurface_set_sync(surface->subsurface);

  surface->ext_session_lock_surface_v1 = ext_session_lock_v1_get_lock_surface(
      s->ext_session_lock_v1, surface->surface, surface->output);
  ext_session_lock_surface_v1_add_listener(
      surface->ext_session_lock_surface_v1,
      &ext_session_lock_surface_v1_listener, surface);

  cfg_t *cfg;
  cfg_get(&cfg);
  if (surface_is_opaque(surface) &&
      cfg->background.mode != BACKGROUND_MODE_CENTER &&
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
  dewlock_surface_t *surface = data;
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

void damage_state(dewlock_state_t *s) {
  dewlock_surface_t *surface;
  wl_list_for_each(surface, &s->surfaces, link) {
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
  dewlock_surface_t *surface = data;
  surface->subpixel = (enum wl_output_subpixel)subpixel;
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
}

static void handle_wl_output_done(void *data, struct wl_output *output) {
  (void)output;
  dewlock_surface_t *surface = data;
  if (!surface->created && surface->state->run_display) {
    create_surface(surface);
  }
}

static void handle_wl_output_scale(void *data, struct wl_output *output,
                                   int32_t factor) {
  (void)output;
  dewlock_surface_t *surface = data;
  surface->scale = factor;
  if (surface->state->run_display) {
    surface->dirty = true;
    render(surface);
  }
}

static void handle_wl_output_name(void *data, struct wl_output *output,
                                  const char *name) {
  (void)output;
  dewlock_surface_t *surface = data;
  surface->output_name = strdup(name);
}

static void handle_wl_output_description(void *data, struct wl_output *output,
                                         const char *description) {
  (void)data;
  (void)output;
  (void)description;
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
  dewlock_state_t *s = data;
  s->locked = true;
}

static void
ext_session_lock_v1_handle_finished(void *data,
                                    struct ext_session_lock_v1 *lock) {
  (void)data;
  (void)lock;
  log_error("Failed to lock session -- "
            "is another lockscreen running?",
            NULL);
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
  dewlock_state_t *s = data;
  if (strcmp(interface, wl_compositor_interface.name) == 0) {
    s->compositor =
        wl_registry_bind(registry, name, &wl_compositor_interface, 4);
  } else if (strcmp(interface, wl_subcompositor_interface.name) == 0) {
    s->subcompositor =
        wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
  } else if (strcmp(interface, wl_shm_interface.name) == 0) {
    s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
  } else if (strcmp(interface, wl_seat_interface.name) == 0) {
    struct wl_seat *seat =
        wl_registry_bind(registry, name, &wl_seat_interface, 4);
    dewlock_seat_t *dewlock_seat = calloc(1, sizeof(dewlock_seat_t));
    dewlock_seat->state = s;
    wl_seat_add_listener(seat, &seat_listener, dewlock_seat);
  } else if (strcmp(interface, wl_output_interface.name) == 0) {
    dewlock_surface_t *surface = calloc(1, sizeof(dewlock_surface_t));
    surface->state = s;
    surface->output = wl_registry_bind(registry, name, &wl_output_interface, 4);
    surface->output_global_name = name;
    wl_output_add_listener(surface->output, &_wl_output_listener, surface);
    wl_list_insert(&s->surfaces, &surface->link);
  } else if (strcmp(interface, ext_session_lock_manager_v1_interface.name) ==
             0) {
    s->ext_session_lock_manager_v1 = wl_registry_bind(
        registry, name, &ext_session_lock_manager_v1_interface, 1);
  }
}

static void handle_global_remove(void *data, struct wl_registry *registry,
                                 uint32_t name) {
  (void)registry;
  dewlock_state_t *s = data;
  dewlock_surface_t *surface;
  wl_list_for_each(surface, &s->surfaces, link) {
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

static cairo_surface_t *select_image(dewlock_state_t *s,
                                     dewlock_surface_t *surface) {
  dewlock_image_t *image;
  cairo_surface_t *default_image = NULL;
  wl_list_for_each(image, &s->images, link) {
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
  if (wl_display_dispatch(state.display) == -1) {
    state.run_display = false;
  }
}

static void comm_in(int fd, short mask, void *data) {
  (void)fd;
  (void)data;
  if (mask & POLLIN) {
    bool auth_success = false;
    if (read_comm_reply(&auth_success) != OK) {
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
    log_error("Password checking subprocess crashed; exiting.", NULL);
    exit(EXIT_FAILURE);
  }
}

static void term_in(int fd, short mask, void *data) {
  (void)fd;
  (void)mask;
  (void)data;
  state.run_display = false;
}

int main(int argc, char **argv) {
  // Parse argv once, fully, before forking the password backend (and, for
  // the shadow backend, dropping setuid root) so the log level is settled
  // beforehand.
  cli_parse(argc, argv);
  cli_opts_t *opts;
  cli_get(&opts);
  log_init(opts->debug ? LOG_LEVEL_DEBUG : LOG_LEVEL_ERROR);

  initialize_pw_backend(argc, argv);
  srand((unsigned int)time(NULL));

  state.failed_attempts = 0;
  wl_list_init(&state.images);

  state.username = getpwuid(getuid())->pw_name;

  char *resolved_config_path = NULL;
  const char *config_path = opts->config;
  if (!config_path) {
    resolved_config_path = cfg_path();
    config_path = resolved_config_path;
  }
  if (config_path) {
    log_debug("Found config at %s", config_path);
  }
  cfg_read(config_path, &state);
  free(resolved_config_path);

  state.password.len = 0;
  state.password.cap = 1024;
  if (password_buffer_create(state.password.cap, &state.password.buf) != OK) {
    return EXIT_FAILURE;
  }
  state.password.buf[0] = 0;

  state_set_time(&state);

  if (pipe(sigusr_fds) != 0) {
    log_error("Failed to pipe", NULL);
    return EXIT_FAILURE;
  }
  if (fcntl(sigusr_fds[1], F_SETFL, O_NONBLOCK) == -1) {
    log_error("Failed to make pipe end nonblocking", NULL);
    return EXIT_FAILURE;
  }

  wl_list_init(&state.surfaces);
  state.xkb.context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  state.display = wl_display_connect(NULL);
  if (!state.display) {
    log_error("Unable to connect to the compositor. "
              "If your compositor is running, check or set the "
              "WAYLAND_DISPLAY environment variable.",
              NULL);
    return EXIT_FAILURE;
  }
  state.eventloop = loop_create();

  struct wl_registry *registry = wl_display_get_registry(state.display);
  wl_registry_add_listener(registry, &registry_listener, &state);
  if (wl_display_roundtrip(state.display) == -1) {
    log_error("wl_display_roundtrip() failed", NULL);
    return EXIT_FAILURE;
  }

  if (!state.compositor || !state.subcompositor || !state.shm ||
      !state.ext_session_lock_manager_v1) {
    log_error("Missing required global", NULL);
    return 1;
  }

  state.ext_session_lock_v1 =
      ext_session_lock_manager_v1_lock(state.ext_session_lock_manager_v1);
  ext_session_lock_v1_add_listener(state.ext_session_lock_v1,
                                   &ext_session_lock_v1_listener, &state);

  if (wl_display_roundtrip(state.display) == -1) {
    return 1;
  }

  dewlock_surface_t *surface;
  wl_list_for_each(surface, &state.surfaces, link) { create_surface(surface); }

  while (!state.locked) {
    if (wl_display_dispatch(state.display) < 0) {
      log_error("wl_display_dispatch() failed", NULL);
      return 2;
    }
  }

  if (opts->ready_fd >= 0) {
    if (write(opts->ready_fd, "\n", 1) != 1) {
      log_error("Failed to send readiness notification", NULL);
      return 2;
    }
    close(opts->ready_fd);
    opts->ready_fd = -1;
  }
  if (opts->daemonize) {
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
