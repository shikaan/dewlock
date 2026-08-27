#include "background-image.h"
#include "cairo.h"
#include "config.h"
#include "dewlock.h"
#include "result.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

static void surface_frame_handle_done(void *data, struct wl_callback *callback,
                                      uint32_t time) {
  (void)time;
  dewlock_surface_t *surface = data;

  wl_callback_destroy(callback);
  surface->frame = NULL;

  render(surface);
}

static const struct wl_callback_listener surface_frame_listener = {
    .done = surface_frame_handle_done,
};

static bool render_frame(dewlock_surface_t *surface);

void render(dewlock_surface_t *surface) {
  assert(surface && "surface must be non-null");
  dewlock_state_t *state = surface->state;
  cfg_t *cfg;
  cfg_get(&cfg);

  int buffer_width = (int)surface->width * surface->scale;
  int buffer_height = (int)surface->height * surface->scale;
  if (buffer_width == 0 || buffer_height == 0) {
    return; // not yet configured
  }

  if (!surface->dirty || surface->frame) {
    // Nothing to do or frame already pending
    return;
  }

  if (buffer_width != surface->last_buffer_width ||
      buffer_height != surface->last_buffer_height) {
    ctx_t *background;
    result_t res = ctx_get_background(state->shm, (uint32_t)buffer_width,
                                      (uint32_t)buffer_height,
                                      surface->buffers, &background);
    if (res != OK) {
      return;
    }

    cairo_t *cairo = background->cairo;
    cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);

    cairo_save(cairo);
    cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_u32(cairo, cfg->colors.background);
    cairo_paint(cairo);
    if (surface->image && cfg->background.mode != BACKGROUND_MODE_SOLID_COLOR) {
      cairo_set_operator(cairo, CAIRO_OPERATOR_OVER);
      render_background_image(cairo, surface->image, cfg->background.mode,
                              buffer_width, buffer_height);
    }
    cairo_restore(cairo);
    cairo_identity_matrix(cairo);

    wl_surface_attach(surface->surface, background->buffer, 0, 0);
    wl_surface_damage_buffer(surface->surface, 0, 0, INT32_MAX, INT32_MAX);

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
  (void)width;
  (void)y;
  assert(cairo && "cairo must be non-null");
  assert(text && "text must be non-null");
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

static void draw_idle(cairo_t *c, dewlock_state_t *state, double h,
                      double w) {
  cfg_t *cfg;
  cfg_get(&cfg);

  const double datey = h / 6;
  const double helpy = h * 5 / 6;
  cairo_text_extents_t extents;

  struct dewlock_text opts = {
      .color = cfg->colors.text,
      .family = cfg->font.family,
  };

  opts.size = cfg->font.size * 6.5;
  opts.weight = CAIRO_FONT_WEIGHT_BOLD;
  draw_text(c, w, datey, opts, state->time, &extents);

  opts.size = cfg->font.size * 2;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  draw_text(c, w, datey + extents.height, opts, state->date, &extents);

  opts.size = cfg->font.size;
  draw_text(c, w, helpy, opts, "Press any key to unlock", &extents);
}

static inline size_t min3u(size_t a, size_t b, size_t c) {
  return a < b ? (a < c ? a : c) : (b < c ? b : c);
}

static inline void set_password(cairo_t *c, dewlock_state_t *state,
                                struct dewlock_text opts, double w, double maxw,
                                char *chars, size_t nchars) {
  assert(chars && "chars must be non-null");
  assert(nchars > 0 && "nchars must be positive");
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

static void draw_form(cairo_t *c, dewlock_state_t *state, double h,
                      double w) {
  cfg_t *cfg;
  cfg_get(&cfg);

  const double inputw = cfg->font.size * 20;
  const double inputpadx = cfg->font.size;
  const double inputh = cfg->font.size * 3;
  const double spacing = cfg->font.size * 1.5;
  const double border = cfg->font.size * 0.25;
  cairo_text_extents_t extents;

  struct dewlock_text opts = {
      .color = cfg->colors.text,
      .family = cfg->font.family,
  };

  opts.size = cfg->font.size * 1.5;
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
  username_opts.size = cfg->font.size * 2.25;
  username_opts.weight = CAIRO_FONT_WEIGHT_BOLD;

  struct dewlock_text message_opts = opts;
  message_opts.size = cfg->font.size;
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
                       ? cfg->colors.error
                       : cfg->colors.text;

  cairo_set_source_u32(c, color);
  cairo_set_line_width(c, border);
  cairo_move_to(c, inputx, inputy + inputh);
  cairo_line_to(c, inputx + inputw, inputy + inputh);
  cairo_stroke(c);

  cairo_set_source_u32(c, cfg->colors.overlay);
  cairo_rectangle(c, inputx, inputy, inputw, inputh);
  cairo_fill(c);

  opts.size = cfg->font.size * 1.5;
  opts.weight = CAIRO_FONT_WEIGHT_NORMAL;
  opts.color = color;
  draw_text(c, w, inputy + spacing + cfg->font.size * 0.5, opts, pwd,
            &extents);

  if (state->auth_state == AUTH_STATE_IDLE && state->xkb.caps_lock) {
    opts.size = cfg->font.size * 0.75;
    opts.weight = CAIRO_FONT_WEIGHT_BOLD;
    opts.color = cfg->colors.warning;
    draw_text(c, w, inputy + inputh + spacing, opts, "CAPS LOCK IS ON",
              &extents);
  }

  message_opts.color = color;
  draw_text(c, w, inputy + inputh + spacing * 3, message_opts, msg, &extents);
}

static bool render_frame(dewlock_surface_t *surface) {
  dewlock_state_t *state = surface->state;
  cfg_t *cfg;
  cfg_get(&cfg);

  // Compute the size of the buffer needed
  int buffer_width = (int)surface->width;
  int buffer_height = (int)surface->height;

  // Ensure buffer size is multiple of buffer scale - required by protocol
  buffer_height += surface->scale - (buffer_height % surface->scale);
  buffer_width += surface->scale - (buffer_width % surface->scale);

  int subsurf_xpos = (int)surface->width / 2 - buffer_width / 2;
  int subsurf_ypos = 0;

  ctx_t *buffer;
  result_t res = ctx_get_indicator(state->shm, (uint32_t)buffer_width,
                                   (uint32_t)buffer_height, surface->buffers,
                                   &buffer);
  if (res != OK) {
    return false;
  }

  // Render the buffer
  cairo_t *cairo = buffer->cairo;
  cairo_set_antialias(cairo, CAIRO_ANTIALIAS_BEST);
  cairo_identity_matrix(cairo);

  // Clear
  cairo_save(cairo);
  cairo_set_source_u32(cairo, cfg->colors.overlay);
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
