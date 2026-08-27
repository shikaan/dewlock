#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

typedef struct loop loop_t;
typedef struct loop_timer loop_timer_t;
typedef struct dewlock_state dewlock_state_t;

typedef struct {
  bool caps_lock;
  bool control;
  struct xkb_state *state;
  struct xkb_context *context;
  struct xkb_keymap *keymap;
} dewlock_xkb_t;

typedef struct {
  dewlock_state_t *state;
  struct wl_pointer *pointer;
  struct wl_keyboard *keyboard;
  int32_t repeat_period_ms;
  int32_t repeat_delay_ms;
  uint32_t repeat_sym;
  uint32_t repeat_codepoint;
  loop_timer_t *repeat_timer;
} dewlock_seat_t;

extern const struct wl_seat_listener seat_listener;
