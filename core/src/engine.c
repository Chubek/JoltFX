#include "jfx/jfx_engine.h"
#include "tilly/tilly.h"
#include <stdlib.h>
#include <string.h>

struct jfx_engine {
    tilly_runtime_t *runtime;
    jfx_engine_config_t config;
};

jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine) {
    if (!config || !out_engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    jfx_engine_t *engine = malloc(sizeof(jfx_engine_t));
    if (!engine) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    engine->runtime = tilly_init(tilly_default_allocator());
    if (!engine->runtime) {
        free(engine);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    memcpy(&engine->config, config, sizeof(jfx_engine_config_t));

    tilly_log(TILLY_LOG_INFO, "JoltFX engine initialized");

    *out_engine = engine;
    return JFX_SUCCESS;
}

void jfx_engine_shutdown(jfx_engine_t *engine) {
    if (engine) {
        tilly_log(TILLY_LOG_INFO, "JoltFX engine shutting down");
        tilly_shutdown(engine->runtime);
        free(engine);
    }
}

jfx_result_t jfx_engine_tick(jfx_engine_t *engine) {
    if (!engine) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    // TODO: Implement frame processing
    return JFX_SUCCESS;
}
