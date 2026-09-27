#ifndef JFX_ENGINE_H
#define JFX_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_engine jfx_engine_t;

typedef enum {
    JFX_SUCCESS = 0,
    JFX_ERROR_INVALID_ARGUMENT = -1,
    JFX_ERROR_OUT_OF_MEMORY = -2,
    JFX_ERROR_NOT_INITIALIZED = -3,
    JFX_ERROR_BACKEND_FAILURE = -4
} jfx_result_t;

typedef struct {
    uint32_t max_buffers;
    uint32_t max_textures;
    uint32_t max_kernels;
    const char *backend_name; // "vulkan", "metal", "d3d12", "webgpu", NULL for auto
} jfx_engine_config_t;

// Initialize the engine
jfx_result_t jfx_engine_init(const jfx_engine_config_t *config, jfx_engine_t **out_engine);

// Shutdown the engine
void jfx_engine_shutdown(jfx_engine_t *engine);

// Process one frame
jfx_result_t jfx_engine_tick(jfx_engine_t *engine);

// Resolved backend name ("vulkan" for NULL/"auto"), NULL for a NULL engine.
const char *jfx_engine_backend_name(const jfx_engine_t *engine);

#ifdef __cplusplus
}
#endif

#endif // JFX_ENGINE_H
