/* NovoGFX backend conformance. The probe assertions branch on whether a
 * DRM/KMS device is actually available, so the test passes headless, in CI,
 * and on real hardware alike. */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "jfx/novogfx_backend.h"
#include "jfx/software_backend.h"

static const char kBrightness[] =
    "(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";

static void near(float actual, float expected) {
    assert(fabsf(actual - expected) < 1e-6f);
}

int main(void) {
    assert(strcmp(jfx_novogfx_backend_name(), "novogfx") == 0);

    jfx_novogfx_backend_t *backend = NULL;
    assert(jfx_novogfx_backend_create(NULL, NULL) == JFX_ERROR_INVALID_ARGUMENT);

    /* A NULL config means "probe": create must succeed with or without a
     * DRM device. */
    assert(jfx_novogfx_backend_create(NULL, &backend) == JFX_SUCCESS && backend);

    jfx_novogfx_caps_t caps;
    memset(&caps, 0, sizeof(caps));
    jfx_novogfx_query_caps(backend, &caps);
    if (caps.gpu_available) {
        /* A device probed: it must be named, must not be mistaken for the
         * CPU fallback, and must report the packed DRM driver version. */
        assert(strstr(caps.device_name, JFX_SOFTWARE_FALLBACK_NAME) == NULL);
        assert(strstr(caps.device_name, "NovoGFX") != NULL);
        assert(caps.api_version != 0);
    } else {
        /* No device: the report must be the honest software fallback. */
        assert(strstr(caps.device_name, JFX_SOFTWARE_FALLBACK_NAME) != NULL);
        assert(caps.api_version == 0);
    }
    /* KMS owns scanout, never shader dispatch: execution is the CPU path
     * either way. */
    assert(!caps.used_gpu);
    assert(!jfx_novogfx_used_gpu(backend));

    jfx_novogfx_query_caps(NULL, &caps);
    assert(!caps.gpu_available && !caps.used_gpu);
    jfx_novogfx_query_caps(backend, NULL);

    /* Execution works regardless of probe outcome. */
    float input[4] = { 0.1f, 0.2f, 0.3f, 0.5f };
    float output[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float amount = 2.0f;
    assert(jfx_novogfx_execute(backend, kBrightness, input, 1, &amount, 1, output) ==
        JFX_SUCCESS);
    near(output[0], 0.2f);
    near(output[1], 0.4f);
    near(output[2], 0.6f);
    near(output[3], 0.5f);
    assert(!jfx_novogfx_used_gpu(backend));

    /* Error paths leave the output untouched. */
    output[0] = 99.0f;
    assert(jfx_novogfx_execute(NULL, kBrightness, input, 1, &amount, 1, output) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(output[0] == 99.0f);
    assert(jfx_novogfx_execute(backend, "(defkernel bad [x] (+ x))", input, 1,
               &amount, 1, output) == JFX_ERROR_INVALID_ARGUMENT);
    assert(output[0] == 99.0f);

    /* The HAL must expose the same behaviour as the typed entry points. */
    const jfx_backend_ops_t *ops = jfx_novogfx_backend_ops();
    assert(ops != NULL);
    assert(strcmp(ops->name(), "novogfx") == 0);
    jfx_novogfx_caps_t typed_caps;
    memset(&typed_caps, 0, sizeof(typed_caps));
    jfx_novogfx_query_caps(backend, &typed_caps);
    jfx_backend_caps_t hal_caps;
    memset(&hal_caps, 0, sizeof(hal_caps));
    ops->query_caps(backend, &hal_caps);
    assert(hal_caps.gpu_available == typed_caps.gpu_available);
    assert(hal_caps.api_version == typed_caps.api_version);
    assert(strcmp(hal_caps.device_name, typed_caps.device_name) == 0);
    assert(!hal_caps.used_gpu);
    float hal_out[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    assert(ops->execute_bytecode(backend, NULL, 0, input, 1, &amount, 1, hal_out) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(hal_out[0] == 0.0f);
    ops->destroy(NULL); /* must tolerate a NULL handle */

    /* An explicit no-probe config must never touch DRM, even on hardware. */
    jfx_novogfx_backend_t *cpu_only = NULL;
    jfx_novogfx_backend_config_t config = { .probe_gpu = false, .memory_limit = 0 };
    assert(jfx_novogfx_backend_create(&config, &cpu_only) == JFX_SUCCESS && cpu_only);
    memset(&caps, 0, sizeof(caps));
    jfx_novogfx_query_caps(cpu_only, &caps);
    assert(!caps.gpu_available && !caps.used_gpu && caps.api_version == 0);
    assert(strstr(caps.device_name, JFX_SOFTWARE_FALLBACK_NAME) != NULL);

    jfx_novogfx_backend_destroy(cpu_only);
    jfx_novogfx_backend_destroy(backend);
    jfx_novogfx_backend_destroy(NULL);
    return 0;
}
