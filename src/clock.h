#pragma once
#include "dewlock.h"
void state_set_time(dewlock_state_t *state);
void schedule_clock_timer(dewlock_state_t *state);
void cancel_clock_timer(dewlock_state_t *state);
