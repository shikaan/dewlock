#include "clock.h"

#include "loop.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

void schedule_clock_timer(dewlock_state_t *state);

void state_set_time(dewlock_state_t *state) {
  assert(state && "state must be non-null");
  static char clock[8] = {0};
  static char date[256] = {0};
  static char month[32] = {0};

  time_t now = time(NULL);
  struct tm *lt = localtime(&now);
  snprintf(clock, sizeof(clock), "%02d:%02d", lt->tm_hour, lt->tm_min);
  state->time = clock;

  strftime(month, sizeof(month), "%B", lt);
  snprintf(date, sizeof(date), "%s %d, %d", month, lt->tm_mday,
           lt->tm_year + 1900);
  state->date = date;
}

static void set_time(void *data) {
  dewlock_state_t *state = data;
  state_set_time(state);
  damage_state(state);
  schedule_clock_timer(state);
}

void schedule_clock_timer(dewlock_state_t *state) {
  assert(state && "state must be non-null");
  if (state->clock_timer) {
    loop_remove_timer(state->eventloop, state->clock_timer);
  }

  state->clock_timer = loop_add_timer(state->eventloop, 1000, set_time, state);
}

void cancel_clock_timer(dewlock_state_t *state) {
  assert(state && "state must be non-null");
  if (state->clock_timer) {
    loop_remove_timer(state->eventloop, state->clock_timer);
    state->clock_timer = NULL;
  }
}
