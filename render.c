#include "background-image.h"
#include "cairo.h"
#include "log.h"
#include "swaylock.h"
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>

static void surface_frame_handle_done(void *data, struct wl_callback *callback,
                                      uint32_t time) {
  struct swaylock_surface *surface = data;

  wl_callback_destroy(callback);
  surface->frame = NULL;

  render(surface);
}

static const struct wl_callback_listener surface_frame_listener = {
    .done = surface_frame_handle_done,
};

static bool render_frame(struct swaylock_surface *surface);

void render(struct swaylock_surface *surface) {
  struct swaylock_state *state = surface->state;

  int buffer_width = surface->width * surface->scale;
  int buffer_height = surface->height * surface->scale;
  if (buffer_width == 0 || buffer_height == 0) {
    return; // not yet configured
  }

  if (!surface->dirty || surface->frame) {
    // Nothing to do or frame already pending
    return;
  }

  bool need_destroy = false;
  struct pool_buffer buffer;

  if (buffer_width != surface->last_buffer_width ||
      buffer_height != surface->last_buffer_height) {
    need_destroy = true;
    if (!create_buffer(state->shm, &buffer, buffer_width, buffer_height,
                       WL_SHM_FORMAT_ARGB8888)) {
      swaylock_log(LOG_ERROR,
                   "Failed to create new buffer for frame background.");
      return;
    }

    cairo_t *cairo = buffer.cairo;
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

    cairo_save(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, state->args.colors.background);
    cairo_paint(cairo);
    if (surface->image && state->args.mode != BACKGROUND_MODE_SOLID_COLOR) {
      cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
      render_background_image(cairo, surface->image, state->args.mode,
                              buffer_width, buffer_height);
    }
    cairo_restore(cairo);
    cairo_identity_matrix(cairo);

    wl_surface_attach(surface->surface, buffer.buffer, 0, 0);
    wl_surface_damage_buffer(surface->surface, 0, 0, INT32_MAX, INT32_MAX);
    need_destroy = true;

    surface->last_buffer_width = buffer_width;
    surface->last_buffer_height = buffer_height;
  }

  // It is possible for the surface scale to change even if the wl_buffer size
  // hasn't
  wl_surface_set_buffer_scale(surface->surface, surface->scale);

  render_frame(surface);
  surface->dirty = false;
  surface->frame = wl_surface_frame(surface->surface);
  wl_callback_add_listener(surface->frame, &surface_frame_listener, surface);
  wl_surface_commit(surface->surface);

  if (need_destroy) {
    destroy_buffer(&buffer);
  }
}

static cairo_font_options_t *font_options = NULL;
static void draw_text(cairo_t *cairo, double x, double y, const char *text,
                      enum wl_output_subpixel subpixel) {
  cairo_text_extents_t extents;
  cairo_font_extents_t fe;

  if (!font_options) {
	  font_options = cairo_font_options_create();
  }

  cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
  cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_SUBPIXEL);
  cairo_font_options_set_subpixel_order(font_options, to_cairo_subpixel_order(subpixel));

  cairo_set_font_options(cairo, font_options);
  // cairo_select_font_face(cairo, state->args.font, CAIRO_FONT_SLANT_NORMAL,
  //                        CAIRO_FONT_WEIGHT_NORMAL);
  cairo_set_font_size(cairo, 16);
  cairo_set_source_rgb(cairo, 1, 1, 1);

  cairo_text_extents(cairo, text, &extents);
  cairo_font_extents(cairo, &fe);

  cairo_move_to(cairo, x, y);
  cairo_show_text(cairo, text);
}

static bool render_frame(struct swaylock_surface *surface) {
  struct swaylock_state *state = surface->state;

  static char password[128];
  for (size_t i = 0; i < state->password.len; i++) {
    password[i] = '*';
  }
  password[state->password.len] = 0;

  // Compute the size of the buffer needed
  int buffer_width = 360;
  int buffer_height = 360;

  // Ensure buffer size is multiple of buffer scale - required by protocol
  buffer_height += surface->scale - (buffer_height % surface->scale);
  buffer_width += surface->scale - (buffer_width % surface->scale);

  int subsurf_xpos = surface->width / 2 - buffer_width / 2;
  int subsurf_ypos = surface->height / 2 - buffer_height / 2;

  struct pool_buffer *buffer = get_next_buffer(
      state->shm, surface->indicator_buffers, buffer_width, buffer_height);
  if (buffer == NULL) {
    swaylock_log(LOG_ERROR, "No buffer");
    return false;
  }

  // Render the buffer
  cairo_t *cairo = buffer->cairo;
  cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

  cairo_identity_matrix(cairo);

  // Clear
  cairo_save(cairo);
  cairo_set_source_rgba(cairo, 0, 0, 0, 0.9);
  cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cairo);
  cairo_restore(cairo);

  // Draw a message
  draw_text(cairo, 24, 24, "manuel", surface->subpixel);
  draw_text(cairo, 24, 48, password, surface->subpixel);
  
  cairo_close_path(cairo);

  // Send Wayland requests
  wl_subsurface_set_position(surface->subsurface, subsurf_xpos, subsurf_ypos);
  wl_surface_set_buffer_scale(surface->child, surface->scale);
  wl_surface_attach(surface->child, buffer->buffer, 0, 0);
  wl_surface_damage_buffer(surface->child, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surface->child);

  return true;
}
