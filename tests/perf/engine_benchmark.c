#include "jfx/jfx_engine.h"

#include <inttypes.h>
#include <stdio.h>

int main(void) {
    enum { frame_count = 1000 };
    jfx_engine_t *engine = NULL;
    jfx_engine_config_t config = { .max_buffers = 1, .backend_name = "webgpu" };
    if (jfx_engine_init(&config, &engine) != JFX_SUCCESS) return 1;
    for (unsigned i = 0; i < frame_count; ++i) {
        if (jfx_engine_tick(engine) != JFX_SUCCESS) { jfx_engine_shutdown(engine); return 1; }
    }
    jfx_engine_metrics_t metrics = { .size = sizeof(metrics) };
    if (jfx_engine_get_metrics(engine, &metrics) != JFX_SUCCESS) {
        jfx_engine_shutdown(engine);
        return 1;
    }
    printf("frames=%" PRIu64 " average_ns=%" PRIu64 " max_ns=%" PRIu64 "\n",
        metrics.frame_count,
        metrics.frame_count ? metrics.total_frame_ns / metrics.frame_count : 0,
        metrics.max_frame_ns);
    jfx_engine_shutdown(engine);
    return 0;
}
