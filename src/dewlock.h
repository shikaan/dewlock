#pragma once
#include "background-image.h"
#include "cairo.h"
#include "pool-buffer.h"
#include "seat.h"
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

// Indicator state: status of authentication attempt
enum auth_state {
  AUTH_STATE_IDLE,       // nothing happening
  AUTH_STATE_VALIDATING, // currently validating password
  AUTH_STATE_INVALID,    // displaying message: password was wrong
};

// Indicator state: status of the password input
enum input_state {
  INPUT_STATE_PRISTINE, // nothing happening; other states decay to this
  INPUT_STATE_DIRTY,    // input was touched
};

// FIXME: this should divide runtime configuration from CLI options
struct dewlock_args {
  struct {
    uint32_t background; // used when image is not specified
    uint32_t overlay;
    uint32_t text;
    uint32_t warning;
    uint32_t error;
  } colors;
  struct {
    char *path;
    enum background_mode mode;
  } background;
  struct {
    const char *family;
    uint32_t size;
  } font;
  bool daemonize;
  int ready_fd;
};

struct dewlock_string {
  size_t len;
  size_t cap;
  char *buf;
};

struct dewlock_state {
  struct loop *eventloop;
  struct loop_timer *input_idle_timer;     // timer to reset input state to IDLE
  struct loop_timer *auth_idle_timer;      // timer to stop displaying error
  struct loop_timer *clear_password_timer; // clears the password buffer
  struct loop_timer *clock_timer;
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct wl_shm *shm;
  struct wl_list surfaces;
  struct wl_list images;
  struct dewlock_args args;
  struct dewlock_string password;
  char *username;
  char *time;
  char *date;
  struct dewlock_xkb xkb;
  enum auth_state auth_state;   // state of the authentication attempt
  enum input_state input_state; // state of the password buffer and key inputs
  int failed_attempts;
  bool run_display, locked;
  struct ext_session_lock_manager_v1 *ext_session_lock_manager_v1;
  struct ext_session_lock_v1 *ext_session_lock_v1;
};

struct dewlock_surface {
  cairo_surface_t *image;
  struct dewlock_state *state;
  struct wl_output *output;
  uint32_t output_global_name;
  struct wl_surface *surface; // surface for background
  struct wl_surface *child;   // indicator surface made into subsurface
  struct wl_subsurface *subsurface;
  struct ext_session_lock_surface_v1 *ext_session_lock_surface_v1;
  struct pool_buffer indicator_buffers[2];
  bool created;
  bool dirty;
  uint32_t width, height;
  int32_t scale;
  enum wl_output_subpixel subpixel;
  char *output_name;
  struct wl_list link;
  struct wl_callback *frame;
  // Dimensions of last wl_buffer committed to background surface
  int last_buffer_width, last_buffer_height;
};

// There is exactly one dewlock_image for each -i argument
struct dewlock_image {
  char *path;
  char *output_name;
  cairo_surface_t *cairo_surface;
  struct wl_list link;
};

void dewlock_handle_key(struct dewlock_state *state, xkb_keysym_t keysym,
                        uint32_t codepoint);

void render(struct dewlock_surface *surface);
void damage_state(struct dewlock_state *state);
void clear_password_buffer(struct dewlock_string *pw);
void schedule_auth_idle(struct dewlock_state *state);

void initialize_pw_backend(int argc, char **argv);
void run_pw_backend_child(void);
// FIXME: declared but never defined anywhere; src/shadow.c calls this and
// fails to link (PAM=0 build only). Was removed at some point in history
// (see `git log -S"clear_buffer"`) without updating callers.
void clear_buffer(char *buf, size_t size);

// Like strcmp, but treats NULL as distinct from (and less than) any string.
int lenient_strcmp(char *a, char *b);
