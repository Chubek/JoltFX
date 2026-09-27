#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "jfx/vk_backend.h"

static const char kBrightness[] =
    "(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";

static void near(float a, float b) {
    assert(fabsf(a - b) < 1e-6f);
}

int main(void) {
    assert(strcmp(jfx_vk_backend_name(), "vulkan") == 0);

    jfx_vk_backend_t *backend = NULL;
    assert(jfx_vk_backend_create(NULL, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_vk_backend_config_t config = { .probe_gpu = false, .memory_limit = 0 };
    assert(jfx_vk_backend_create(&config, &backend) == JFX_SUCCESS && backend);

    jfx_vk_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    jfx_vk_query_caps(backend, &caps);
    assert(caps.cpu_fallback);
    assert(!caps.gpu_available && caps.api_version == 0);
    assert(strcmp(caps.device_name, "cpu-fallback") == 0);
    jfx_vk_query_caps(NULL, &caps);
    assert(caps.cpu_fallback);

    float input[8] = {0.1f, 0.2f, 0.3f, 0.5f, 0.4f, 0.3f, 0.2f, 1.0f};
    float out[8] = {0};
    float amount = 2.0f;
    assert(jfx_vk_execute(backend, kBrightness, input, 2, &amount, 1, out) == JFX_SUCCESS);
    near(out[0], 0.2f);
    near(out[1], 0.4f);
    near(out[2], 0.6f);
    near(out[3], 0.5f);
    near(out[4], 0.8f);
    near(out[7], 1.0f);

    /* In-place execution is safe. */
    float inout[4] = {0.1f, 0.2f, 0.3f, 0.5f};
    assert(jfx_vk_execute(backend, kBrightness, inout, 1, &amount, 1, inout) == JFX_SUCCESS);
    near(inout[0], 0.2f);

    /* Error paths leave the output untouched. */
    out[0] = 99.0f;
    assert(jfx_vk_execute(NULL, kBrightness, input, 2, &amount, 1, out) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vk_execute(backend, NULL, input, 2, &amount, 1, out) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vk_execute(backend, kBrightness, input, 0, &amount, 1, out) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_vk_execute(backend, "(defkernel broken [x] (+ x))", input, 2, &amount, 1,
               out) == JFX_ERROR_INVALID_ARGUMENT);
    assert(out[0] == 99.0f);

    /* An undersized scratch budget fails cleanly instead of truncating. */
    jfx_vk_backend_t *tiny = NULL;
    jfx_vk_backend_config_t tiny_config = { .probe_gpu = false, .memory_limit = 1 };
    assert(jfx_vk_backend_create(&tiny_config, &tiny) == JFX_SUCCESS);
    assert(jfx_vk_execute(tiny, kBrightness, input, 2, &amount, 1, out) ==
        JFX_ERROR_BACKEND_FAILURE);
    assert(out[0] == 99.0f);
    jfx_vk_backend_destroy(tiny);

    jfx_vk_backend_destroy(backend);
    jfx_vk_backend_destroy(NULL);

    /* Default configuration probes the driver. When a device is present the
     * same program must produce the same pixels on the GPU path. */
    backend = NULL;
    assert(jfx_vk_backend_create(NULL, &backend) == JFX_SUCCESS && backend);
    memset(&caps, 0, sizeof(caps));
    jfx_vk_query_caps(backend, &caps);
    float gpu_out[8] = {0};
    assert(jfx_vk_execute(backend, kBrightness, input, 2, &amount, 1, gpu_out) ==
        JFX_SUCCESS);
    near(gpu_out[0], 0.2f);
    near(gpu_out[1], 0.4f);
    near(gpu_out[2], 0.6f);
    near(gpu_out[3], 0.5f);
    near(gpu_out[4], 0.8f);
    near(gpu_out[7], 1.0f);
    if (caps.gpu_available) {
        assert(strcmp(caps.device_name, "cpu-fallback") != 0);
        assert(jfx_vk_used_gpu(backend));
    } else {
        assert(!jfx_vk_used_gpu(backend));
    }
    jfx_vk_backend_destroy(backend);
    return 0;
}
