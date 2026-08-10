#include "comm.h"
#include "loop.h"
#include "seat.h"
#include "dewlock.h"
#include "unicode.h"
#include <assert.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

// FIXME: rearrange code around password and its buffer. 
//        This function could be in password-buffer
void clear_password_buffer(struct dewlock_string *pw) {
  memset(pw->buf, 0, pw->cap);
  pw->len = 0;
}

static bool backspace(struct dewlock_string *pw) {
  if (pw->len != 0) {
    pw->len -= utf8_last_size(pw->buf);
    pw->buf[pw->len] = 0;
    return true;
  }
  return false;
}

static void append_ch(struct dewlock_string *pw, uint32_t codepoint) {
  size_t utf8_size = utf8_chsize(codepoint);
  if (pw->len + utf8_size + 1 >= pw->cap) {
    // TODO: Display error
    return;
  }
  utf8_encode(&pw->buf[pw->len], codepoint);
  pw->buf[pw->len + utf8_size] = 0;
  pw->len += utf8_size;
}

static void set_auth_idle(void *data) {
  struct dewlock_state *state = data;
  state->auth_idle_timer = NULL;
  state->auth_state = AUTH_STATE_IDLE;
  damage_state(state);
}

static void cancel_input_idle(struct dewlock_state *state) {
  if (state->input_idle_timer) {
    loop_remove_timer(state->eventloop, state->input_idle_timer);
    state->input_idle_timer = NULL;
  }
}

void schedule_auth_idle(struct dewlock_state *state) {
  if (state->auth_idle_timer) {
    loop_remove_timer(state->eventloop, state->auth_idle_timer);
  }
  state->auth_idle_timer =
      loop_add_timer(state->eventloop, 3000, set_auth_idle, state);
}

static void clear_password(void *data) {
  struct dewlock_state *state = data;
  state->clear_password_timer = NULL;
  state->input_state = INPUT_STATE_PRISTINE;
  clear_password_buffer(&state->password);
  damage_state(state);
}

static void schedule_password_clear(struct dewlock_state *state) {
  if (state->clear_password_timer) {
    loop_remove_timer(state->eventloop, state->clear_password_timer);
  }
  state->clear_password_timer =
      loop_add_timer(state->eventloop, 10000, clear_password, state);
}

static void cancel_password_clear(struct dewlock_state *state) {
  if (state->clear_password_timer) {
    loop_remove_timer(state->eventloop, state->clear_password_timer);
    state->clear_password_timer = NULL;
  }
}

static void submit_password(struct dewlock_state *state) {
  if (state->auth_state == AUTH_STATE_VALIDATING) {
    return;
  }

  state->input_state = INPUT_STATE_PRISTINE;
  state->auth_state = AUTH_STATE_VALIDATING;
  cancel_password_clear(state);
  cancel_input_idle(state);

  if (!write_comm_request(&state->password)) {
    state->auth_state = AUTH_STATE_INVALID;
    schedule_auth_idle(state);
  }

  damage_state(state);
}

void dewlock_handle_key(struct dewlock_state *state, xkb_keysym_t keysym,
                         uint32_t codepoint) {

	// Do not accept inputs while validating
	if (state->auth_state == AUTH_STATE_VALIDATING) return;

  switch (keysym) {
  case XKB_KEY_KP_Enter: /* fallthrough */
  case XKB_KEY_Return:
    submit_password(state);
    break;
  case XKB_KEY_Delete:
  case XKB_KEY_BackSpace:
    if (state->xkb.control) {
      clear_password_buffer(&state->password);
      cancel_password_clear(state);
    } else {
      if (backspace(&state->password) && state->password.len != 0) {
        schedule_password_clear(state);
      } else {
        cancel_password_clear(state);
      }
    }
    damage_state(state);
    break;
  case XKB_KEY_Escape:
    clear_password_buffer(&state->password);
    state->input_state = INPUT_STATE_PRISTINE;
    cancel_password_clear(state);
    damage_state(state);
    break;
  case XKB_KEY_Caps_Lock:
  case XKB_KEY_Shift_L:
  case XKB_KEY_Shift_R:
  case XKB_KEY_Control_L:
  case XKB_KEY_Control_R:
  case XKB_KEY_Meta_L:
  case XKB_KEY_Meta_R:
  case XKB_KEY_Alt_L:
  case XKB_KEY_Alt_R:
  case XKB_KEY_Super_L:
  case XKB_KEY_Super_R:
    schedule_password_clear(state);
    damage_state(state);
    break;
  case XKB_KEY_m: /* fallthrough */
  case XKB_KEY_d:
  case XKB_KEY_j:
    if (state->xkb.control) {
      submit_password(state);
      break;
    }
    // fallthrough
  case XKB_KEY_c: /* fallthrough */
  case XKB_KEY_u:
    if (state->xkb.control) {
      state->input_state = INPUT_STATE_DIRTY;
      clear_password_buffer(&state->password);
      cancel_password_clear(state);
      damage_state(state);
      break;
    }
    // fallthrough
  default:
    if (codepoint) {
			if (state->auth_state == AUTH_STATE_INVALID) {
				state->auth_state = AUTH_STATE_IDLE;
			}
      append_ch(&state->password, codepoint);
      state->input_state = INPUT_STATE_DIRTY;
      schedule_password_clear(state);
      damage_state(state);
    }
    break;
  }
}
