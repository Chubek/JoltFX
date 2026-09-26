#include "jfx/jfx_engine.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    jfx_engine_config_t config = {
        .max_buffers = 1024,
        .max_textures = 256,
        .max_kernels = 128,
        .backend_name = NULL
    };

    jfx_engine_t *engine = NULL;
    jfx_result_t result = jfx_engine_init(&config, &engine);

    assert(result == JFX_SUCCESS);
    assert(engine != NULL);

    jfx_engine_shutdown(engine);

    printf("Core engine tests passed\n");
    return 0;
}
