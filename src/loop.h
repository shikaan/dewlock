#pragma once
#include <stdbool.h>

typedef struct loop loop_t;
typedef struct loop_timer loop_timer_t;

loop_t *loop_create(void);
void loop_destroy(loop_t *loop);
void loop_poll(loop_t *loop);
void loop_add_fd(loop_t *loop, int fd, short mask,
                 void (*func)(int fd, short mask, void *data), void *data);
loop_timer_t *loop_add_timer(loop_t *loop, int ms,
                             void (*callback)(void *data), void *data);
bool loop_remove_fd(loop_t *loop, int fd);
bool loop_remove_timer(loop_t *loop, loop_timer_t *timer);
