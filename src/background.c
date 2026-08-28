#include "background.h"
#include "log.h"
#include <assert.h>
#include <cairo/cairo.h>
#include <string.h>

background_mode_t bg_parse_mode(const char *mode) {
  assert(mode && "mode must be non-null");
  if (strcmp(mode, "stretch") == 0) {
    return BACKGROUND_MODE_STRETCH;
  } else if (strcmp(mode, "fill") == 0) {
    return BACKGROUND_MODE_FILL;
  } else if (strcmp(mode, "fit") == 0) {
    return BACKGROUND_MODE_FIT;
  } else if (strcmp(mode, "center") == 0) {
    return BACKGROUND_MODE_CENTER;
  } else if (strcmp(mode, "tile") == 0) {
    return BACKGROUND_MODE_TILE;
  } else if (strcmp(mode, "solid_color") == 0) {
    return BACKGROUND_MODE_SOLID_COLOR;
  }
  log_error("Unsupported background mode: %s", mode);
  return BACKGROUND_MODE_INVALID;
}

cairo_surface_t *bg_load_image(const char *path) {
  assert(path && "path must be non-null");
  cairo_surface_t *image = cairo_image_surface_create_from_png(path);
  if (!image) {
    log_error("Failed to read background image.", NULL);
    return NULL;
  }
  if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS) {
    log_error("Failed to read background image: %s.",
              cairo_status_to_string(cairo_surface_status(image)));
    return NULL;
  }
  return image;
}

void bg_render_image(cairo_t *cairo, cairo_surface_t *image,
                      background_mode_t mode, int buffer_width,
                      int buffer_height) {
  assert(cairo && "cairo must be non-null");
  assert(image && "image must be non-null");
  assert(buffer_width > 0 && "buffer_width must be positive");
  assert(buffer_height > 0 && "buffer_height must be positive");
  double width = cairo_image_surface_get_width(image);
  double height = cairo_image_surface_get_height(image);

  cairo_save(cairo);
  switch (mode) {
  case BACKGROUND_MODE_STRETCH:
    cairo_scale(cairo, (double)buffer_width / width,
                (double)buffer_height / height);
    cairo_set_source_surface(cairo, image, 0, 0);
    break;
  case BACKGROUND_MODE_FILL: {
    double window_ratio = (double)buffer_width / buffer_height;
    double bg_ratio = width / height;

    if (window_ratio > bg_ratio) {
      double scale = (double)buffer_width / width;
      cairo_scale(cairo, scale, scale);
      cairo_set_source_surface(cairo, image, 0,
                               (double)buffer_height / 2 / scale - height / 2);
    } else {
      double scale = (double)buffer_height / height;
      cairo_scale(cairo, scale, scale);
      cairo_set_source_surface(cairo, image,
                               (double)buffer_width / 2 / scale - width / 2, 0);
    }
    break;
  }
  case BACKGROUND_MODE_FIT: {
    double window_ratio = (double)buffer_width / buffer_height;
    double bg_ratio = width / height;

    if (window_ratio > bg_ratio) {
      double scale = (double)buffer_height / height;
      cairo_scale(cairo, scale, scale);
      cairo_set_source_surface(cairo, image,
                               (double)buffer_width / 2 / scale - width / 2, 0);
    } else {
      double scale = (double)buffer_width / width;
      cairo_scale(cairo, scale, scale);
      cairo_set_source_surface(cairo, image, 0,
                               (double)buffer_height / 2 / scale - height / 2);
    }
    break;
  }
  case BACKGROUND_MODE_CENTER:
    /*
     * Align the unscaled image to integer pixel boundaries
     * in order to prevent loss of clarity (this only matters
     * for odd-sized images).
     */
    cairo_set_source_surface(cairo, image,
                             (int)((double)buffer_width / 2 - width / 2),
                             (int)((double)buffer_height / 2 - height / 2));
    break;
  case BACKGROUND_MODE_TILE: {
    cairo_pattern_t *pattern = cairo_pattern_create_for_surface(image);
    cairo_pattern_set_extend(pattern, CAIRO_EXTEND_REPEAT);
    cairo_set_source(cairo, pattern);
    break;
  }
  case BACKGROUND_MODE_SOLID_COLOR:
  case BACKGROUND_MODE_INVALID:
  default:
    assert(0 && "unreachable: invalid background mode");
    break;
  }
  cairo_paint(cairo);
  cairo_restore(cairo);
}
