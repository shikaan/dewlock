#pragma once
#include <stdbool.h>

/**
 * This is an event loop system designed for sway clients, not sway itself.
 *
 * The loop consists of file descriptors and timers. Typically the Wayland
 * display's file descriptor will be one of the fds in the loop.
 */

typedef struct loop loop_t;
typedef struct loop_timer loop_timer_t;

/**
 * Create an event loop.
 */
loop_t *loop_create(void);

/**
 * Destroy the event loop (eg. on program termination).
 */
void loop_destroy(loop_t *loop);

/**
 * Poll the event loop. This will block until one of the fds has data.
 */
void loop_poll(loop_t *loop);

/**
 * Add a file descriptor to the loop.
 */
void loop_add_fd(loop_t *loop, int fd, short mask,
                 void (*func)(int fd, short mask, void *data), void *data);

/**
 * Add a timer to the loop.
 *
 * When the timer expires, the timer will be removed from the loop and freed.
 */
loop_timer_t *loop_add_timer(loop_t *loop, int ms,
                             void (*callback)(void *data), void *data);

/**
 * Remove a file descriptor from the loop.
 */
bool loop_remove_fd(loop_t *loop, int fd);

/**
 * Remove a timer from the loop.
 */
bool loop_remove_timer(loop_t *loop, loop_timer_t *timer);
