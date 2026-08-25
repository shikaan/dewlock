#include "background-image.h"
#include "cairo.h"
#include "dewlock.h"
#include "log.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-client.h>

static void surface_frame_handle_done(void *data, struct wl_callback *callback,
                                      uint32_t time) {
  struct dewlock_surface *surface = data;

  wl_callback_destroy(callback);
  surface->frame = NULL;

  render(surface);
}

static const struct wl_callback_listener surface_frame_listener = {
    .done = surface_frame_handle_done,
};

static bool render_frame(struct dewlock_surface *surface);

void render(struct dewlock_surface *surface) {
  struct dewlock_state *state = surface->state;

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
      dewlock_log(LOG_ERROR,
                  "Failed to create new buffer for frame background.%s", "");
      return;
    }

    cairo_t *cairo = buffer.cairo;
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

    cairo_save(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, state->args.colors.background);
    cairo_paint(cairo);
    if (surface->image &&
        state->args.background.mode != BACKGROUND_MODE_SOLID_COLOR) {
      cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
      render_background_image(cairo, surface->image,
                              state->args.background.mode, buffer_width,
                              buffer_height);
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

struct dewlock_text {
  uint32_t color;
  double size;
  const char *family;
  cairo_font_weight_t weight;
};

static void init_text(cairo_t *cairo, double width, double y,
                      struct dewlock_text opts, const char *text,
                      cairo_text_extents_t *extents) {
  assert(extents && "extents must be non-null");

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
  cairo_set_source_u32(cairo, opts.color);
  cairo_text_extents(cairo, text, extents);
}

static void draw_text(cairo_t *cairo, double width, double y,
                      struct dewlock_text opts, const char *text,
                      cairo_text_extents_t *extents) {
  init_text(cairo, width, y, opts, text, extents);
  cairo_move_to(cairo, width / 2 - extents->width / 2, y);
  cairo_show_text(cairo, text);
}

static void draw_idle(cairo_t *c, struct dewlock_state *state, double h,
                      double w) {
  const double datey = h / 6;
  const double helpy = h * 5 / 6;
  cairo_text_extents_t extents;

  struct dewlock_text opts = {
      .color = state->args.colors.text,
      .family = state->args.font.family,
  };

  opts.size = state->args.font.size * 6.5;
  opts.weight = CAIRO_FONT_WEIGHT_BOLD;
  draw_text(c, w, datey, opts, state->time, &extents);

  opts.size = state->args.font.size * 2;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  draw_text(c, w, datey + extents.height, opts, state->date, &extents);

  opts.size = state->args.font.size;
  draw_text(c, w, helpy, opts, "Press any key to unlock", &extents);
}

static inline size_t min3u(size_t a, size_t b, size_t c) {
  return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

static inline void set_password(cairo_t *c, struct dewlock_state *state,
                                struct dewlock_text opts, double w, double maxw,
                                char *chars, size_t nchars) {
  static size_t last_len;
  cairo_text_extents_t glyph_extents;
  init_text(c, w, 0, opts, "*", &glyph_extents);
  size_t maxlen = (size_t)(maxw / glyph_extents.x_advance);

  size_t len = state->auth_state == AUTH_STATE_VALIDATING
                   ? last_len
                   : min3u(state->password.len, maxlen, nchars - 1);
  last_len = len;
  memset(chars, '*', len);
  chars[len] = 0;
}

static void draw_form(cairo_t *c, struct dewlock_state *state, double h,
                      double w) {
  const double inputw = state->args.font.size * 20;
  const double inputpadx = state->args.font.size;
  const double inputh = state->args.font.size * 3;
  const double spacing = state->args.font.size * 1.5;
  const double border = state->args.font.size * 0.25;
  cairo_text_extents_t extents;

  struct dewlock_text opts = {
      .color = state->args.colors.text,
      .family = state->args.font.family,
  };

  opts.size = state->args.font.size * 1.5;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;

  static char pwd[128];
  set_password(c, state, opts, w, inputw - inputpadx * 2, pwd, sizeof(pwd));

  const char *msg = "";
  if (state->auth_state == AUTH_STATE_IDLE) {
    msg = "Press Enter to submit";
  } else if (state->auth_state == AUTH_STATE_VALIDATING) {
    msg = "Verifying...";
  } else {
    msg = "Invalid credentials. Try again";
  }

  struct dewlock_text username_opts = opts;
  username_opts.size = state->args.font.size * 2.25;
  username_opts.weight = CAIRO_FONT_WEIGHT_BOLD;

  struct dewlock_text message_opts = opts;
  message_opts.size = state->args.font.size;
  message_opts.weight = CAIRO_FONT_WEIGHT_NORMAL;

  cairo_text_extents_t username_extents, message_extents;
  init_text(c, w, 0, username_opts, state->username, &username_extents);
  init_text(c, w, 0, message_opts, msg, &message_extents);

  const double form_height =
      -username_extents.y_bearing + username_extents.height + spacing + inputh +
      spacing * 3 + message_extents.y_bearing + message_extents.height;
  const double formy = h / 2 - form_height / 2 - username_extents.y_bearing;

  draw_text(c, w, formy, username_opts, state->username, &extents);

  const double inputx = w / 2 - inputw / 2;
  const double inputy = formy + extents.height + spacing;

  uint32_t color = state->auth_state == AUTH_STATE_INVALID
                       ? state->args.colors.error
                       : state->args.colors.text;

  cairo_set_source_u32(c, color);
  cairo_set_line_width(c, border);
  cairo_move_to(c, inputx, inputy + inputh);
  cairo_line_to(c, inputx + inputw, inputy + inputh);
  cairo_stroke(c);

  cairo_set_source_u32(c, state->args.colors.overlay);
  cairo_rectangle(c, inputx, inputy, inputw, inputh);
  cairo_fill(c);

  opts.size = state->args.font.size * 1.5;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  opts.color = color;
  draw_text(c, w, inputy + spacing + state->args.font.size * 0.5, opts, pwd,
            &extents);

  if (state->auth_state == AUTH_STATE_IDLE && state->xkb.caps_lock) {
    opts.size = state->args.font.size * 0.75;
    opts.weight = CAIRO_FONT_WEIGHT_BOLD;
    opts.color = state->args.colors.warning;
    draw_text(c, w, inputy + inputh + spacing, opts, "CAPS LOCK IS ON",
              &extents);
  }

  message_opts.color = color;
  draw_text(c, w, inputy + inputh + spacing * 3, message_opts, msg, &extents);
}

static bool render_frame(struct dewlock_surface *surface) {
  struct dewlock_state *state = surface->state;

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
    dewlock_log(LOG_ERROR, "No buffer%s", "");
    return false;
  }

  // Render the buffer
  cairo_t *cairo = buffer->cairo;
  cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
  cairo_identity_matrix(cairo);

  // Clear
  cairo_save(cairo);
  cairo_set_source_u32(cairo, state->args.colors.overlay);
  cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
  cairo_paint(cairo);
  cairo_restore(cairo);

  if (state->input_state == INPUT_STATE_PRISTINE &&
      state->auth_state == AUTH_STATE_IDLE) {
    draw_idle(cairo, state, buffer_height, buffer_width);
  } else {
    draw_form(cairo, state, buffer_height, buffer_width);
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
