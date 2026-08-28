#pragma once
#include "dewlock.h"
#include <xkbcommon/xkbcommon.h>

void pwd_handle_key(dewlock_state_t *state, xkb_keysym_t keysym,
                    uint32_t codepoint);
void pwd_clear_buffer(dewlock_string_t *pw);
void pwd_schedule_auth_idle(dewlock_state_t *state);
