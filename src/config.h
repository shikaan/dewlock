#pragma once
#include "background-image.h"
#include "dewlock.h"
#include <stdint.h>

typedef struct {
  struct {
    uint32_t background; // used when image is not specified
    uint32_t overlay;
    uint32_t text;
    uint32_t warning;
    uint32_t error;
  } colors;
  struct {
    char *path;
    background_mode_t mode;
  } background;
  struct {
    const char *family;
    uint32_t size;
  } font;
} cfg_t;

char *cfg_path(void);
void cfg_read(const char *path, dewlock_state_t *state);
void cfg_get(cfg_t **cfg);
