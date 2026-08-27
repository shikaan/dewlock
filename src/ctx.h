#pragma once
#include "result.h"
#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

// pool[0] holds the background context, recreated only when the surface is
// resized. pool[1] and pool[2] are double-buffered and hold the indicator
// context, redrawn on every frame.
enum { CTX_POOL_SIZE = 3 };

typedef struct {
  struct wl_buffer *buffer;
  cairo_surface_t *surface;
  cairo_t *cairo;
  uint32_t width, height;
  void *data;
  size_t size;
  bool busy;
} ctx_t;

result_t ctx_get_background(struct wl_shm *shm, uint32_t width,
                            uint32_t height, ctx_t pool[static CTX_POOL_SIZE],
                            ctx_t **ctx);
result_t ctx_get_indicator(struct wl_shm *shm, uint32_t width,
                           uint32_t height, ctx_t pool[static CTX_POOL_SIZE],
                           ctx_t **ctx);
void ctx_deinit(ctx_t pool[static CTX_POOL_SIZE]);
