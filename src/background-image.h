#pragma once
#include "cairo.h"

typedef enum {
  BACKGROUND_MODE_STRETCH,
  BACKGROUND_MODE_FILL,
  BACKGROUND_MODE_FIT,
  BACKGROUND_MODE_CENTER,
  BACKGROUND_MODE_TILE,
  BACKGROUND_MODE_SOLID_COLOR,
  BACKGROUND_MODE_INVALID,
} background_mode_t;

background_mode_t parse_background_mode(const char *mode);
cairo_surface_t *load_background_image(const char *path);
void render_background_image(cairo_t *cairo, cairo_surface_t *image,
                             background_mode_t mode, int buffer_width,
                             int buffer_height);
