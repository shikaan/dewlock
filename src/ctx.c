#include "ctx.h"
#include "log.h"
#include "result.h"
#include <assert.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>

static int anonymous_shm_open(void) {
  int retries = 100;

  do {
    // try a probably-unique name
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    pid_t pid = getpid();
    char name[50];
    snprintf(name, sizeof(name), "/dewlock-%x-%x", (unsigned int)pid,
             (unsigned int)ts.tv_nsec);

    // shm_open guarantees that O_CLOEXEC is set
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd >= 0) {
      shm_unlink(name);
      return fd;
    }

    --retries;
  } while (retries > 0 && errno == EEXIST);

  return -1;
}

static void buffer_release(void *data, struct wl_buffer *wl_buffer) {
  (void)wl_buffer;
  ctx_t *ctx = data;
  ctx->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {.release =
                                                              buffer_release};

static void ctx_destroy(ctx_t *ctx) {
  assert(ctx && "ctx must be non-null");
  if (ctx->buffer) {
    wl_buffer_destroy(ctx->buffer);
  }
  if (ctx->cairo) {
    cairo_destroy(ctx->cairo);
  }
  if (ctx->surface) {
    cairo_surface_destroy(ctx->surface);
  }
  if (ctx->data) {
    munmap(ctx->data, ctx->size);
  }
  memset(ctx, 0, sizeof(ctx_t));
}

static result_t ctx_create(struct wl_shm *shm, int32_t width, int32_t height,
                           uint32_t format, ctx_t *ctx) {
  assert(shm && "shm must be non-null");
  assert(ctx && "ctx must be non-null");
  assert(width >= 0 && "width must be non-negative");
  assert(height >= 0 && "height must be non-negative");
  uint32_t stride = (uint32_t)width * 4;
  size_t size = stride * (uint32_t)height;

  void *data = NULL;
  if (size > 0) {
    int fd = anonymous_shm_open();
    if (fd == -1) {
      log_error("failed to open anonymous shm: %s", strerror(errno));
      return ERR_CTX_ALLOCATION;
    }
    if (ftruncate(fd, (off_t)size) < 0) {
      log_error("failed to truncate shm: %s", strerror(errno));
      close(fd);
      return ERR_CTX_ALLOCATION;
    }
    data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *shm_pool = wl_shm_create_pool(shm, fd, (int32_t)size);
    ctx->buffer = wl_shm_pool_create_buffer(shm_pool, 0, width, height,
                                            (int32_t)stride, format);
    wl_buffer_add_listener(ctx->buffer, &buffer_listener, ctx);
    wl_shm_pool_destroy(shm_pool);
    close(fd);
  }

  ctx->size = size;
  ctx->width = (uint32_t)width;
  ctx->height = (uint32_t)height;
  ctx->data = data;
  ctx->surface = cairo_image_surface_create_for_data(
      data, CAIRO_FORMAT_ARGB32, width, height, (int)stride);
  cairo_status_t status = cairo_surface_status(ctx->surface);
  if (status != CAIRO_STATUS_SUCCESS) {
    log_error("cairo error: %s", cairo_status_to_string(status));
    return ERR_CTX_CAIRO;
  }
  ctx->cairo = cairo_create(ctx->surface);
  return OK;
}

result_t ctx_get_background(struct wl_shm *shm, uint32_t width,
                            uint32_t height, ctx_t pool[static CTX_POOL_SIZE],
                            ctx_t **ctx) {
  assert(shm && "shm must be non-null");
  assert(ctx && "ctx must be non-null");
  ctx_t *background = &pool[0];

  if (background->width != width || background->height != height) {
    ctx_destroy(background);
    result_t res = ctx_create(shm, (int32_t)width, (int32_t)height,
                              WL_SHM_FORMAT_ARGB8888, background);
    if (res != OK) {
      *ctx = NULL;
      return res;
    }
  }

  *ctx = background;
  return OK;
}

result_t ctx_get_indicator(struct wl_shm *shm, uint32_t width,
                           uint32_t height, ctx_t pool[static CTX_POOL_SIZE],
                           ctx_t **ctx) {
  assert(shm && "shm must be non-null");
  assert(ctx && "ctx must be non-null");
  ctx_t *selected = pool[1].busy ? (pool[2].busy ? NULL : &pool[2]) : &pool[1];

  if (!selected) {
    log_warn("no free %ux%u indicator buffer to draw", width, height);
    *ctx = NULL;
    return ERR_CTX_NO_BUFFERS;
  }

  if (selected->width != width || selected->height != height) {
    ctx_destroy(selected);
  }

  if (!selected->buffer) {
    result_t res = ctx_create(shm, (int32_t)width, (int32_t)height,
                              WL_SHM_FORMAT_ARGB8888, selected);
    if (res != OK) {
      *ctx = NULL;
      return res;
    }
  }

  selected->busy = true;
  *ctx = selected;
  return OK;
}

void ctx_deinit(ctx_t pool[static CTX_POOL_SIZE]) {
  for (size_t i = 0; i < CTX_POOL_SIZE; ++i) {
    ctx_destroy(&pool[i]);
  }
}
