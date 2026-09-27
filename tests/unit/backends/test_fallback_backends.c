#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "jfx/d3d12_backend.h"
#include "jfx/mtl_backend.h"
#include "jfx/software_backend.h"
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
        /* These backends run a CPU path: no device, and the fallback must  */                      \
        /* be named so a capability report is never mistaken for a GPU.   */                       \
        assert(!caps.gpu_available && !caps.used_gpu && caps.api_version == 0);                    \
        assert(strstr(caps.device_name, JFX_SOFTWARE_FALLBACK_NAME) != NULL);                   \
        prefix##_query_caps(NULL, &caps);                                                           \
        assert(!caps.gpu_available && !caps.used_gpu);                                             \
        prefix##_query_caps(backend, NULL);                                                         \
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
        /* The HAL must expose the same behaviour as the typed entry points. */                    \
        const jfx_backend_ops_t *ops = prefix##_backend_ops();                                     \
        assert(ops != NULL);                                                                        \
        char expected_name[16];                                                                     \
        snprintf(expected_name, sizeof(expected_name), "%s", prefix##_backend_name());             \
        assert(strcmp(ops->name(), expected_name) == 0);                                           \
        jfx_backend_caps_t hal_caps;                                                                \
        memset(&hal_caps, 0, sizeof(hal_caps));                                                     \
        ops->query_caps(backend, &hal_caps);                                                        \
        assert(!hal_caps.gpu_available && !hal_caps.used_gpu);                                     \
        assert(strstr(hal_caps.device_name, JFX_SOFTWARE_FALLBACK_NAME) != NULL);                    \
        float hal_out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };                                             \
        assert(ops->execute_bytecode(backend, NULL, 0, input, 1, &amount, 1, hal_out) ==            \
            JFX_ERROR_INVALID_ARGUMENT);                                                           \
        assert(hal_out[0] == 0.0f);                                                                 \
        ops->destroy(NULL); /* must tolerate a NULL handle */                                       \
        prefix##_backend_destroy(backend);                                                          \
        prefix##_backend_destroy(NULL);                                                             \
    } while (0)

int main(void) {
    /* One conformance run per adapter that this build actually contains. */
    int checked = 0;
#if defined(JFX_TEST_BACKEND_METAL) && JFX_TEST_BACKEND_METAL
    CHECK_BACKEND(jfx_mtl, jfx_mtl_backend_t, jfx_mtl_backend_config_t, jfx_mtl_caps_t);
    ++checked;
#endif
#if defined(JFX_TEST_BACKEND_D3D12) && JFX_TEST_BACKEND_D3D12
    CHECK_BACKEND(jfx_d3d12, jfx_d3d12_backend_t, jfx_d3d12_backend_config_t, jfx_d3d12_caps_t);
    ++checked;
#endif
#if defined(JFX_TEST_BACKEND_WEBGPU) && JFX_TEST_BACKEND_WEBGPU
    CHECK_BACKEND(jfx_wgpu, jfx_wgpu_backend_t, jfx_wgpu_backend_config_t, jfx_wgpu_caps_t);
    ++checked;
#endif
    /* A build with no adapter adapters has nothing to check here; the CMake
     * target only exists in that case, so reaching zero is a logic error. */
    assert(checked > 0);
    return 0;
}
