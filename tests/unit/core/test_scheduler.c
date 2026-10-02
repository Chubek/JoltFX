#include "jfx/jfx_scheduler.h"
#include <assert.h>
#include <stdatomic.h>

static atomic_uint completed;
static void count_task(void *data) {
    assert(data == &completed);
    atomic_fetch_add(&completed, 1);
}

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
static unsigned order[8], used;
static void ordered_task(void *data) {
    order[used++] = *(unsigned *)data;
}
static void submit_child(void *data) {
    count_task(data);
    assert(scheduler_submit(count_task, data, 1));
}
#endif

int main(void) {
    assert(!scheduler_submit(count_task, &completed, 0));
    assert(scheduler_init(2));
    assert(!scheduler_init(1));
    assert(!scheduler_submit(NULL, NULL, 0));
    for (unsigned i = 0; i < 64; ++i) assert(scheduler_submit(count_task, &completed, (int)i));
    scheduler_wait_idle();
    assert(atomic_load(&completed) == 64);
    assert(scheduler_pending_tasks() == 0);

#if defined(__EMSCRIPTEN__) && !defined(__EMSCRIPTEN_PTHREADS__)
    unsigned ids[] = {0, 1, 2, 3};
    assert(scheduler_submit(ordered_task, &ids[0], -2));
    assert(scheduler_submit(ordered_task, &ids[1], 5));
    assert(scheduler_submit(ordered_task, &ids[2], 5));
    assert(scheduler_submit(ordered_task, &ids[3], 1));
    assert(scheduler_pending_tasks() == 4);
    scheduler_wait_idle();
    assert(used == 4 && order[0] == 1 && order[1] == 2 && order[2] == 3 && order[3] == 0);
    assert(scheduler_submit(submit_child, &completed, 0));
    scheduler_wait_idle();
    assert(atomic_load(&completed) == 66);

    // No workers race this capacity assertion in a serial module.
    for (unsigned i = 0; i < 1024; ++i) assert(scheduler_submit(count_task, &completed, 0));
    assert(!scheduler_submit(count_task, &completed, 0));
    assert(scheduler_pending_tasks() == 1024);
    scheduler_wait_idle();
    assert(atomic_load(&completed) == 1090);
#endif

    unsigned before = atomic_load(&completed);
    assert(scheduler_submit(count_task, &completed, 0));
    scheduler_shutdown();
    assert(atomic_load(&completed) == before + 1);
    assert(!scheduler_submit(count_task, &completed, 0));
    assert(scheduler_pending_tasks() == 0);
    scheduler_wait_idle();
    assert(scheduler_init(0));
    scheduler_shutdown();
    scheduler_shutdown();
    return 0;
}
