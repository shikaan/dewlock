#pragma once
#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

typedef struct {
  struct wl_buffer *buffer;
  cairo_surface_t *surface;
  cairo_t *cairo;
  uint32_t width, height;
  void *data;
  size_t size;
  bool busy;
} pool_buffer_t;

pool_buffer_t *create_buffer(struct wl_shm *shm, pool_buffer_t *buf,
                             int32_t width, int32_t height, uint32_t format);
pool_buffer_t *get_next_buffer(struct wl_shm *shm, pool_buffer_t pool[static 2],
                               uint32_t width, uint32_t height);
void destroy_buffer(pool_buffer_t *buffer);
