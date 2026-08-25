#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <xkbcommon/xkbcommon.h>

struct loop;
struct loop_timer;

struct dewlock_xkb {
  bool caps_lock;
  bool control;
  struct xkb_state *state;
  struct xkb_context *context;
  struct xkb_keymap *keymap;
};

struct dewlock_seat {
  struct dewlock_state *state;
  struct wl_pointer *pointer;
  struct wl_keyboard *keyboard;
  int32_t repeat_period_ms;
  int32_t repeat_delay_ms;
  uint32_t repeat_sym;
  uint32_t repeat_codepoint;
  struct loop_timer *repeat_timer;
};

extern const struct wl_seat_listener seat_listener;
