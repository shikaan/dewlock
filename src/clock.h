#pragma once
#include "dewlock.h"
void clk_set_time(dewlock_state_t *state);
void clk_schedule_timer(dewlock_state_t *state);
void clk_cancel_timer(dewlock_state_t *state);
