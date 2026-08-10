#ifndef _SWAY_CLOCK_H
#define _SWAY_CLOCK_H
#include "dewlock.h"
void state_set_time(struct dewlock_state* state);
void schedule_clock_timer(struct dewlock_state *state);
void cancel_clock_timer(struct dewlock_state *state);
#endif
