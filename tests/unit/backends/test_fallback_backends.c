#include <assert.h>
#include <math.h>
#include <string.h>

#include "jfx/d3d12_backend.h"
#include "jfx/mtl_backend.h"
#include "jfx/wgpu_backend.h"

static const char kBrightness[] =
    "(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";

static void near(float actual, float expected) {
    assert(fabsf(actual - expected) < 1e-6f);
}

#define CHECK_BACKEND(prefix, backend_type, config_type, caps_type)                              \
    do {                                                                                            \
        backend_type *backend = NULL;                                                               \
        config_type config = { .probe_gpu = false, .memory_limit = 0 };                           \
        caps_type caps;                                                                             \
        float input[4] = { 0.1f, 0.2f, 0.3f, 0.5f };                                              \
        float output[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                                             \
        float amount = 2.0f;                                                                        \
        assert(prefix##_backend_create(NULL, NULL) == JFX_ERROR_INVALID_ARGUMENT);                \
        assert(prefix##_backend_create(&config, &backend) == JFX_SUCCESS && backend);             \
        memset(&caps, 0, sizeof(caps));                                                             \
        prefix##_query_caps(backend, &caps);                                                        \
        assert(caps.cpu_fallback && !caps.gpu_available);                                          \
        assert(strcmp(caps.device_name, "software-fallback") == 0);                               \
        assert(prefix##_execute(backend, kBrightness, input, 1, &amount, 1, output) ==             \
            JFX_SUCCESS);                                                                           \
        near(output[0], 0.2f);                                                                      \
        near(output[1], 0.4f);                                                                      \
        near(output[2], 0.6f);                                                                      \
        near(output[3], 0.5f);                                                                      \
        assert(!prefix##_used_gpu(backend));                                                        \
        output[0] = 99.0f;                                                                          \
        assert(prefix##_execute(backend, "(defkernel bad [x] (+ x))", input, 1, &amount, 1,      \
                   output) == JFX_ERROR_INVALID_ARGUMENT);                                        \
        assert(output[0] == 99.0f);                                                                 \
        prefix##_backend_destroy(backend);                                                          \
    } while (0)

int main(void) {
    CHECK_BACKEND(jfx_mtl, jfx_mtl_backend_t, jfx_mtl_backend_config_t, jfx_mtl_caps_t);
    CHECK_BACKEND(jfx_d3d12, jfx_d3d12_backend_t, jfx_d3d12_backend_config_t, jfx_d3d12_caps_t);
    CHECK_BACKEND(jfx_wgpu, jfx_wgpu_backend_t, jfx_wgpu_backend_config_t, jfx_wgpu_caps_t);
    return 0;
}
