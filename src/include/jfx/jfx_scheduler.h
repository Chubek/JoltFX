#ifndef JFX_SCHEDULER_H
#define JFX_SCHEDULER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize scheduler with worker thread count. Non-pthread Emscripten builds
// drain the bounded priority queue on the caller in wait_idle/tick/shutdown.
bool scheduler_init(uint32_t worker_count);

// Shutdown scheduler
void scheduler_shutdown(void);

// Submit a task to the bounded queue; false if full. Higher priorities run first.
bool scheduler_submit(void (*func)(void *), void *userdata, int priority);

// Get number of pending tasks
uint32_t scheduler_pending_tasks(void);

// Wait for both queued and currently executing tasks to finish
void scheduler_wait_idle(void);

#ifdef __cplusplus
}
#endif

#endif // JFX_SCHEDULER_H
