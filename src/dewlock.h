#pragma once
#include "ctx.h"
#include "seat.h"
#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

// Indicator state: status of authentication attempt
typedef enum {
  AUTH_STATE_IDLE,       // nothing happening
  AUTH_STATE_VALIDATING, // currently validating password
  AUTH_STATE_INVALID,    // displaying message: password was wrong
} auth_state_t;

// Indicator state: status of the password input
typedef enum {
  INPUT_STATE_PRISTINE, // nothing happening; other states decay to this
  INPUT_STATE_DIRTY,    // input was touched
} input_state_t;

typedef struct dewlock_string dewlock_string_t;
struct dewlock_string {
  size_t len;
  size_t cap;
  char *buf;
};

typedef struct dewlock_state dewlock_state_t;
struct dewlock_state {
  loop_t *eventloop;
  loop_timer_t *input_idle_timer;     // timer to reset input state to IDLE
  loop_timer_t *auth_idle_timer;      // timer to stop displaying error
  loop_timer_t *clear_password_timer; // clears the password buffer
  loop_timer_t *clock_timer;
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct wl_shm *shm;
  struct wl_list surfaces;
  struct wl_list images;
  dewlock_string_t password;
  char *username;
  char *time;
  char *date;
  dewlock_xkb_t xkb;
  auth_state_t auth_state;   // state of the authentication attempt
  input_state_t input_state; // state of the password buffer and key inputs
  int failed_attempts;
  bool run_display, locked;
  struct ext_session_lock_manager_v1 *ext_session_lock_manager_v1;
  struct ext_session_lock_v1 *ext_session_lock_v1;
};

typedef struct {
  cairo_surface_t *image;
  dewlock_state_t *state;
  struct wl_output *output;
  uint32_t output_global_name;
  struct wl_surface *surface; // surface for background
  struct wl_surface *child;   // indicator surface made into subsurface
  struct wl_subsurface *subsurface;
  struct ext_session_lock_surface_v1 *ext_session_lock_surface_v1;
  ctx_t buffers[CTX_POOL_SIZE];
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
} dewlock_surface_t;

// There is exactly one dewlock_image_t for each -i argument
typedef struct {
  char *path;
  char *output_name;
  cairo_surface_t *cairo_surface;
  struct wl_list link;
} dewlock_image_t;

void damage_state(dewlock_state_t *state);
