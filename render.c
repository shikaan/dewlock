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

struct swaylock_text {
  double color[4];
  double size;
  char *family;
  cairo_font_weight_t weight;
};

static void draw_text(cairo_t *cairo, double width, double y,
                      struct swaylock_text opts, const char *text,
                      cairo_text_extents_t *extents) {
  static cairo_font_options_t *font_options = NULL;
  if (!font_options) {
    font_options = cairo_font_options_create();
  }

  cairo_font_options_set_hint_style(font_options, CAIRO_HINT_STYLE_FULL);
  cairo_font_options_set_antialias(font_options, CAIRO_ANTIALIAS_SUBPIXEL);

  cairo_set_font_options(cairo, font_options);
  cairo_select_font_face(cairo, opts.family, CAIRO_FONT_SLANT_NORMAL,
                         opts.weight);
  cairo_set_font_size(cairo, opts.size);
  cairo_set_source_rgba(cairo, opts.color[0], opts.color[1], opts.color[2],
                        opts.color[3]);

  cairo_text_extents(cairo, text, extents);

  cairo_move_to(cairo, width / 2 - extents->width / 2, y);
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
  int buffer_width = surface->width;
  int buffer_height = surface->height;

  // Ensure buffer size is multiple of buffer scale - required by protocol
  buffer_height += surface->scale - (buffer_height % surface->scale);
  buffer_width += surface->scale - (buffer_width % surface->scale);

  int subsurf_xpos = surface->width / 2 - buffer_width / 2;
  int subsurf_ypos = 0;

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
  cairo_set_source_rgba(cairo, 0, 0, 0, 0.3);
  cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cairo);
  cairo_restore(cairo);

  // Text drawing
  double form_position = (double)buffer_height * 2 / 3;
  double date_position = (double)buffer_height / 6;

  cairo_text_extents_t extents;
  struct swaylock_text time = {
      .color = {1, 1, 1, 1},
      .size = 104,
      .family = "Noto Sans",
      .weight = CAIRO_FONT_WEIGHT_BOLD,
  };
  draw_text(cairo, buffer_width, date_position, time, state->time.buf,
            &extents);

  struct swaylock_text date = {
      .color = {1, 1, 1, 1},
      .size = 32,
      .family = "Noto Sans",
  };
  draw_text(cairo, buffer_width,
            date_position + extents.height, date,
            state->date.buf, &extents);

  struct swaylock_text form = {
      .color = {1, 1, 1, 1},
      .size = 18,
      .family = "Noto Sans",
      .weight = CAIRO_FONT_WEIGHT_NORMAL,
  };

  if (state->password.len == 0) {
    const char *msg;
    switch (state->auth_state) {
    case AUTH_STATE_IDLE: {
      msg = "Press any key to unlock";
      break;
    }
    case AUTH_STATE_VALIDATING: {
      msg = "Validating...";
      break;
    }
    case AUTH_STATE_INVALID:
    default:
      msg = "";
    }
    draw_text(cairo, buffer_width, form_position + form.size + 16, form, msg,
              &extents);
  } else {
    draw_text(cairo, buffer_width, form_position, form, state->username.buf,
              &extents);
    draw_text(cairo, buffer_width, form_position + form.size + 16, form,
              password, &extents);
  }
  cairo_close_path(cairo);

  // Send Wayland requests
  wl_subsurface_set_position(surface->subsurface, subsurf_xpos, subsurf_ypos);
  wl_surface_set_buffer_scale(surface->child, surface->scale);
  wl_surface_attach(surface->child, buffer->buffer, 0, 0);
  wl_surface_damage_buffer(surface->child, 0, 0, INT32_MAX, INT32_MAX);
  wl_surface_commit(surface->child);

  return true;
}
