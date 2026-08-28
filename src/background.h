#pragma once
#include <cairo/cairo.h>

typedef enum {
  BACKGROUND_MODE_STRETCH,
  BACKGROUND_MODE_FILL,
  BACKGROUND_MODE_FIT,
  BACKGROUND_MODE_CENTER,
  BACKGROUND_MODE_TILE,
  BACKGROUND_MODE_SOLID_COLOR,
  BACKGROUND_MODE_INVALID,
} background_mode_t;

background_mode_t bg_parse_mode(const char *mode);
cairo_surface_t *bg_load_image(const char *path);
void bg_render_image(cairo_t *cairo, cairo_surface_t *image,
                      background_mode_t mode, int buffer_width,
                      int buffer_height);
