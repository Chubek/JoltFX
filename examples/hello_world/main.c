#include "jfx/jfx_engine.h"
#include "tilly/logger.h"

int main(void) {
    tilly_log_simple(TILLY_LOG_INFO, "Hello from JoltFX!");

    jfx_engine_config_t config = {
        .max_buffers = 256,
        .max_textures = 64,
        .max_kernels = 32,
        .backend_name = NULL
    };

    jfx_engine_t *engine = NULL;
    if (jfx_engine_init(&config, &engine) != JFX_SUCCESS) {
        return 1;
    }

    tilly_log_simple(TILLY_LOG_INFO, "Engine initialized successfully");

    jfx_engine_shutdown(engine);

    return 0;
}
