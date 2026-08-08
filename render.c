#include "background-image.h"
#include "cairo.h"
#include "log.h"
#include "swaylock.h"
#include <assert.h>
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
  uint32_t color;
  double size;
  char *family;
  cairo_font_weight_t weight;
};

static void init_text(cairo_t *cairo, double width, double y,
                      struct swaylock_text opts, const char *text,
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
                      struct swaylock_text opts, const char *text,
                      cairo_text_extents_t *extents) {
  init_text(cairo, width, y, opts, text, extents);
  cairo_move_to(cairo, width / 2 - extents->width / 2, y);
  cairo_show_text(cairo, text);
}

static void draw_idle(cairo_t *c, struct swaylock_state *state, double h,
                      double w) {
  const double datey = h / 6;
  const double helpy = h * 5 / 6;
  cairo_text_extents_t extents;

  struct swaylock_text opts = {
      .color = 0xffffffff,
      .family = "Noto Sans",
  };

  opts.size = 104;
  opts.weight = CAIRO_FONT_WEIGHT_BOLD;
  draw_text(c, w, datey, opts, state->time.buf, &extents);

  opts.size = 32;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  draw_text(c, w, datey + extents.height, opts, state->date.buf, &extents);

  opts.size = 16;
  draw_text(c, w, helpy, opts, "Press any key to unlock", &extents);
}

static inline size_t min3u(size_t a, size_t b, size_t c) {
  return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

static inline void set_password(cairo_t *c, struct swaylock_state *state,
                                struct swaylock_text opts, double w,
                                double maxw, char *chars, size_t nchars) {
  static size_t last_len;
  cairo_text_extents_t glyph_extents;
  init_text(c, w, 0, opts, "*", &glyph_extents);
  size_t maxlen = (size_t)(maxw / glyph_extents.x_advance);

  size_t len = state->auth_state == AUTH_STATE_VALIDATING
                   ? last_len
                   : min3u(state->password.len, maxlen, nchars - 1);
  last_len = len;
  for (size_t i = 0; i < len; i++) {
    chars[i] = '*';
  }
  chars[len] = 0;
}

static void draw_form(cairo_t *c, struct swaylock_state *state, double h,
                      double w) {
  const double formy = h / 2 - 64; // FIXME: calculate form size and move up
  const double inputw = 320;
  const double inputpadx = 16;
  cairo_text_extents_t extents;

  uint32_t white = 0xffffffff;
  uint32_t yellow = 0xffdd00ff;
  uint32_t red = 0xcc6566ff;

  struct swaylock_text opts = {
      .color = white,
      .family = "Noto Sans",
  };

  opts.size = 24;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;

  static char pwd[128];
  set_password(c, state, opts, w, inputw - inputpadx * 2, pwd, sizeof(pwd));

  opts.size = 36;
  opts.weight = CAIRO_FONT_WEIGHT_BOLD;
  draw_text(c, w, formy, opts, state->username.buf, &extents);

  const double inputh = 48;
  const double spacing = 24;
  const double border = 4;
  const double inputx = w / 2 - inputw / 2;
  const double inputy = formy + extents.height + spacing;

  uint32_t color = state->auth_state == AUTH_STATE_INVALID ? red : white;

  cairo_set_source_u32(c, color);
  cairo_set_line_width(c, border);
  cairo_move_to(c, inputx, inputy + inputh);
  cairo_line_to(c, inputx + inputw, inputy + inputh);
  cairo_stroke(c);

  cairo_set_source_rgba(c, 0.01, 0.01, 0.01, 1);
  cairo_rectangle(c, inputx, inputy, inputw, inputh);
  cairo_fill(c);

  opts.size = 24;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  opts.color = color;
  draw_text(c, w, inputy + spacing + 8, opts, pwd, &extents);

  if (state->auth_state == AUTH_STATE_IDLE && state->xkb.caps_lock) {
    opts.size = 12;
    opts.weight = CAIRO_FONT_WEIGHT_BOLD;
    opts.color = yellow;
    draw_text(c, w, inputy + inputh + spacing, opts, "CAPS LOCK IS ON",
              &extents);
  }

  opts.size = 16;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  opts.color = color;

  const char *msg = "";
  if (state->auth_state == AUTH_STATE_IDLE) {
    msg = "Press Enter to submit";
  } else if (state->auth_state == AUTH_STATE_VALIDATING) {
    msg = "Verifying...";
  } else {
    msg = "Invalid credentials. Try again";
  }

  draw_text(c, w, inputy + inputh + spacing * 3, opts, msg, &extents);
}

static bool render_frame(struct swaylock_surface *surface) {
  struct swaylock_state *state = surface->state;

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
