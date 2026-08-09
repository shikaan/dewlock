#ifndef _SWAYLOCK_H
#define _SWAYLOCK_H
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
  INPUT_STATE_PRISTINE, // nothing happening; other states decay to this after
                        // time
  INPUT_STATE_DIRTY,    // input was touched
};

// FIXME: this is unneeded
struct swaylock_colors {
  uint32_t background; // used when image is not specified
  uint32_t overlay;
  uint32_t text;
  uint32_t warning;
  uint32_t error;
};

// FIXME: this should divide runtime configuration from CLI options
struct swaylock_args {
  struct swaylock_colors colors;
  struct {
    char *path;
    enum background_mode mode;
  } background;
  struct {
    char *family;
    uint32_t size;
  } font;
  bool daemonize;
  int ready_fd;
};

struct swaylock_string {
  size_t len;
  size_t cap;
  char *buf;
};

struct swaylock_state {
  struct loop *eventloop;
  struct loop_timer *input_idle_timer; // timer to reset input state to IDLE
  struct loop_timer
      *auth_idle_timer; // timer to stop displaying AUTH_STATE_INVALID
  struct loop_timer *clear_password_timer; // clears the password buffer
  struct loop_timer *clock_timer;
  struct wl_display *display;
  struct wl_compositor *compositor;
  struct wl_subcompositor *subcompositor;
  struct wl_shm *shm;
  struct wl_list surfaces;
  struct wl_list images;
  struct swaylock_args args;
  struct swaylock_string password;
  char* username;
  char* time;
  char* date;
  struct swaylock_xkb xkb;
  enum auth_state auth_state;   // state of the authentication attempt
  enum input_state input_state; // state of the password buffer and key inputs
  int failed_attempts;
  bool run_display, locked;
  struct ext_session_lock_manager_v1 *ext_session_lock_manager_v1;
  struct ext_session_lock_v1 *ext_session_lock_v1;
};

struct swaylock_surface {
  cairo_surface_t *image;
  struct swaylock_state *state;
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

// There is exactly one swaylock_image for each -i argument
struct swaylock_image {
  char *path;
  char *output_name;
  cairo_surface_t *cairo_surface;
  struct wl_list link;
};

void swaylock_handle_key(struct swaylock_state *state, xkb_keysym_t keysym,
                         uint32_t codepoint);

void render(struct swaylock_surface *surface);
void damage_state(struct swaylock_state *state);
void clear_password_buffer(struct swaylock_string *pw);
void schedule_auth_idle(struct swaylock_state *state);

void initialize_pw_backend(int argc, char **argv);
void run_pw_backend_child(void);
void clear_buffer(char *buf, size_t size);

#endif
