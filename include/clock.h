#ifndef _SWAY_CLOCK_H
#define _SWAY_CLOCK_H
#include "swaylock.h"
void state_set_time(struct swaylock_state* state);
void schedule_clock_timer(struct swaylock_state *state);
void cancel_clock_timer(struct swaylock_state *state);
#endif
