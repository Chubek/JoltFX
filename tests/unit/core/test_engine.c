#include "jfx/jfx_engine.h"
#include "jfx/jfx_memory.h"
#include "jfx/jfx_scheduler.h"
#include "jfx/jfx_events.h"
#include "tilly/allocator.h"
#include "jfx_test_backend.h"
#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ready = PTHREAD_COND_INITIALIZER;
static int released;
static atomic_int finished;
static int calls;

static void task(void *unused) {
    (void)unused;
    pthread_mutex_lock(&gate);
    while (!released) pthread_cond_wait(&ready, &gate);
    pthread_mutex_unlock(&gate);
    atomic_fetch_add(&finished, 1);
}
static void handler(jfx_event_type_t type, void *data, void *userdata) {
    (void)data; (void)userdata;
    assert(type == JFX_EVENT_FRAME_BEGIN);
    ++calls;
    assert(event_unsubscribe(type, handler));
}
int main(void) {
    jfx_engine_t *engine = (jfx_engine_t *)1;
    assert(jfx_engine_init(NULL, &engine) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_engine_config_t cfg = {.max_buffers = 1};
    assert(jfx_engine_init(&cfg, &engine) == JFX_SUCCESS && engine);
    assert(!event_subscribe((jfx_event_type_t)-1, handler, NULL));
    assert(event_subscribe(JFX_EVENT_FRAME_BEGIN, handler, NULL));
    event_publish(JFX_EVENT_FRAME_BEGIN, NULL);
    assert(calls == 1 && event_subscriber_count(JFX_EVENT_FRAME_BEGIN) == 0);
    assert(scheduler_submit(task, NULL, 10));
    pthread_mutex_lock(&gate);
    released = 1;
    pthread_cond_broadcast(&ready);
    pthread_mutex_unlock(&gate);
    scheduler_wait_idle();
    assert(atomic_load(&finished) == 1);
    assert(jfx_engine_tick(engine) == JFX_SUCCESS);
    jfx_engine_metrics_t metrics = {.size = sizeof(metrics)};
    assert(jfx_engine_get_metrics(engine, &metrics) == JFX_SUCCESS);
    assert(metrics.frame_count == 1 && metrics.total_frame_ns >= metrics.last_frame_ns);
    metrics.size = 0;
    assert(jfx_engine_get_metrics(engine, &metrics) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frame_alloc(32, 16));
    assert(jfx_frame_arena_usage() >= 32);
    jfx_frame_reset();
    assert(jfx_frame_arena_usage() == 0);
    void *block = jfx_resource_alloc(32, 8);
    assert(block && jfx_resource_pool_usage() == 64);
    jfx_resource_free(block);
    assert(jfx_resource_pool_usage() == 0);
    jfx_engine_shutdown(engine);
    assert(jfx_engine_init(&cfg, &engine) == JFX_SUCCESS);
    assert(event_subscriber_count(JFX_EVENT_FRAME_BEGIN) == 0);
    assert(jfx_engine_backend_name(engine) != NULL);
    jfx_engine_shutdown(engine);
    assert(jfx_engine_backend_name(NULL) == NULL);
    jfx_engine_config_t bogus = {.max_buffers = 1, .backend_name = "directx9"};
    engine = NULL;
    assert(jfx_engine_init(&bogus, &engine) == JFX_ERROR_INVALID_ARGUMENT && !engine);
    jfx_engine_config_t named = {.max_buffers = 1, .backend_name = jfx_test_backend()};
    assert(jfx_engine_init(&named, &engine) == JFX_SUCCESS && engine);
    assert(strcmp(jfx_engine_backend_name(engine), jfx_test_backend()) == 0);
    assert(jfx_engine_tick(engine) == JFX_SUCCESS);
    jfx_engine_shutdown(engine);
    named.backend_name = "auto";
    assert(jfx_engine_init(&named, &engine) == JFX_SUCCESS && engine);
    /* "auto" must resolve to one of the backends this build contains. */
    assert(strcmp(jfx_engine_backend_name(engine), "vulkan") == 0 ||
        strcmp(jfx_engine_backend_name(engine), "metal") == 0 ||
        strcmp(jfx_engine_backend_name(engine), "d3d12") == 0 ||
        strcmp(jfx_engine_backend_name(engine), "webgpu") == 0);
    jfx_engine_shutdown(engine);
    /* A named backend is only offered when it was actually built, so selection
     * is checked against the backends this configuration contains. */
    int named_checked = 0;
#if defined(JFX_BACKEND_VULKAN)
    named.backend_name = "vulkan";
    assert(jfx_engine_init(&named, &engine) == JFX_SUCCESS &&
        strcmp(jfx_engine_backend_name(engine), "vulkan") == 0);
    jfx_engine_shutdown(engine);
    ++named_checked;
#endif
#if defined(JFX_BACKEND_METAL)
    named.backend_name = "metal";
    assert(jfx_engine_init(&named, &engine) == JFX_SUCCESS &&
        strcmp(jfx_engine_backend_name(engine), "metal") == 0);
    jfx_engine_shutdown(engine);
    ++named_checked;
#endif
#if defined(JFX_BACKEND_D3D12)
    named.backend_name = "d3d12";
    assert(jfx_engine_init(&named, &engine) == JFX_SUCCESS &&
        strcmp(jfx_engine_backend_name(engine), "d3d12") == 0);
    jfx_engine_shutdown(engine);
    ++named_checked;
#endif
#if defined(JFX_BACKEND_WEBGPU)
    named.backend_name = "webgpu";
    assert(jfx_engine_init(&named, &engine) == JFX_SUCCESS &&
        strcmp(jfx_engine_backend_name(engine), "webgpu") == 0);
    jfx_engine_shutdown(engine);
    ++named_checked;
#endif
    assert(named_checked > 0);
    /* A backend this build does not contain is rejected, not silently mapped. */
    named.backend_name = "no_such_backend";
    engine = NULL;
    assert(jfx_engine_init(&named, &engine) == JFX_ERROR_INVALID_ARGUMENT && !engine);
    tilly_allocator_t *alloc = tilly_allocator_create(TILLY_ALLOC_GENERAL, 1024);
    assert(alloc);
    assert(!tilly_alloc(alloc, 16, 3));
    void *ptr = tilly_alloc(alloc, 16, 8);
    assert(ptr);
    ptr = tilly_realloc(alloc, ptr, 32);
    assert(ptr);
    tilly_free(alloc, ptr);
    tilly_allocator_destroy(alloc);
    puts("Core foundation tests passed");
    return 0;
}
