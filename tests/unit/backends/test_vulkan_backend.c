#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "jfx/vk_backend.h"

static const char kBrightness[] =
    "(defkernel brightness [r g b a amount] (rgba (* r amount) (* g amount) (* b amount) a))";
static const char kBindings[] =
    "(defkernel bindings [r g b a amount] (rgba "
    "(do (var v r) (set! v (* v amount)) v) "
    "(let [v (* g amount)] v) (do (let v b) (* v amount)) a))";

static void near(float a, float b) {
    assert(fabsf(a - b) < 1e-6f);
}

static void conditionals(jfx_vk_backend_t *backend) {
    const char *typed="(defkernel typed [r g b a] (var n 0) (for [i 0 4] (set! n (+ n i))) (rgba (+ r (f64 n)) g b a))";
    float typed_in[4]={0.25f,0.5f,0.75f,1},typed_out[4]={0};
    assert(jfx_vk_execute(backend,typed,typed_in,1,NULL,0,typed_out)==JFX_SUCCESS);
    near(typed_out[0],6.25f);near(typed_out[1],0.5f);near(typed_out[2],0.75f);near(typed_out[3],1);
    assert(!jfx_vk_used_gpu(backend));
    const char *source = "(defkernel k [r g b a] (rgba "
        "(if (> r 0) r (if (< r 0) 0.5 (/ 1 0))) "
        "(do (var v g) (when b (set! v (+ v 0.25))) v) "
        "(cond [(= b 0) 0.75] [else (unless 0 b)]) a))";
    float in[] = {0.1f, 0.2f, 0.3f, 1, -0.1f, 0.2f, 0, 0.5f};
    float out[8];
    assert(jfx_vk_execute(backend, source, in, 2, NULL, 0, out) == JFX_SUCCESS);
    near(out[0], 0.1f); near(out[1], 0.45f); near(out[2], 0.3f); near(out[3], 1);
    near(out[4], 0.5f); near(out[5], 0.2f); near(out[6], 0.75f); near(out[7], 0.5f);
    for (unsigned i = 0; i < 8; ++i) out[i] = 99;
    in[0] = 0; /* selected numerical failure must not publish partial output */
    assert(jfx_vk_execute(backend, source, in, 2, NULL, 0, out) != JFX_SUCCESS);
    for (unsigned i = 0; i < 8; ++i) near(out[i], 99);
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
    assert(!caps.gpu_available && caps.api_version == 0);
    assert(!caps.used_gpu);
    assert(strcmp(caps.device_name, "cpu-fallback") == 0);
    jfx_vk_query_caps(NULL, &caps);
    assert(!caps.gpu_available && strcmp(caps.device_name, "cpu-fallback") == 0);
    jfx_vk_query_caps(backend, NULL);
    conditionals(backend);

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
    assert(jfx_vk_execute(backend, kBindings, input, 2, &amount, 1, out) == JFX_SUCCESS);
    near(out[0], 0.2f); near(out[1], 0.4f); near(out[2], 0.6f); near(out[7], 1.f);

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
    conditionals(backend);
    assert(jfx_vk_execute(backend, kBrightness, input, 2, &amount, 1, gpu_out) ==
        JFX_SUCCESS);
    near(gpu_out[0], 0.2f);
    near(gpu_out[1], 0.4f);
    near(gpu_out[2], 0.6f);
    near(gpu_out[3], 0.5f);
    near(gpu_out[4], 0.8f);
    near(gpu_out[7], 1.0f);
    assert(jfx_vk_execute(backend, kBindings, input, 2, &amount, 1, gpu_out) == JFX_SUCCESS);
    near(gpu_out[0], 0.2f); near(gpu_out[1], 0.4f); near(gpu_out[2], 0.6f); near(gpu_out[7], 1.f);
    if (caps.gpu_available) {
        assert(strcmp(caps.device_name, "cpu-fallback") != 0);
        assert(caps.api_version != 0);
        assert(jfx_vk_used_gpu(backend));
        /* The HAL must agree with the typed entry point. */
        const jfx_backend_ops_t *ops = jfx_vk_backend_ops();
        assert(ops && strcmp(ops->name(), "vulkan") == 0);
        jfx_backend_caps_t hal_caps;
        memset(&hal_caps, 0, sizeof(hal_caps));
        ops->query_caps(backend, &hal_caps);
        assert(hal_caps.gpu_available && hal_caps.used_gpu);
        assert(strcmp(hal_caps.device_name, caps.device_name) == 0);

        /* An unwritable shader cache is not a dispatch failure. A full or
         * read-only XDG_CACHE_HOME must degrade to scratch files in TMPDIR and
         * still produce the same pixels on the GPU path. */
        char ro[] = "/tmp/joltfx-ro-cache-XXXXXX";
        /* mkdtemp needs _GNU_SOURCE/_DEFAULT_SOURCE; declare it locally to keep
         * the test self-contained regardless of the project's feature macros. */
        extern char *mkdtemp(char *);
        char *ro_made = mkdtemp(ro);
        if (ro_made) {
            const char *saved_cache = getenv("XDG_CACHE_HOME");
            const char *saved_tmp = getenv("TMPDIR");
            char saved_tmp_buf[4096];
            bool had_tmp = saved_tmp != NULL;
            if (had_tmp) {
                snprintf(saved_tmp_buf, sizeof(saved_tmp_buf), "%s", saved_tmp);
            }
            assert(chmod(ro, 0500) == 0);
            setenv("XDG_CACHE_HOME", ro, 1);
            /* glslc writes its output next to the source, so the scratch
             * directory must stay usable for the fallback to work. */
            if (had_tmp) {
                setenv("TMPDIR", saved_tmp_buf, 1);
            } else {
                unsetenv("TMPDIR");
            }
            float ro_out[8] = {0};
            assert(jfx_vk_execute(backend, kBrightness, input, 2, &amount, 1,
                       ro_out) == JFX_SUCCESS);
            near(ro_out[0], 0.2f);
            near(ro_out[4], 0.8f);
            near(ro_out[7], 1.0f);
            if (had_tmp) {
                setenv("TMPDIR", saved_tmp_buf, 1);
            } else {
                unsetenv("TMPDIR");
            }
            if (saved_cache) {
                setenv("XDG_CACHE_HOME", saved_cache, 1);
            } else {
                unsetenv("XDG_CACHE_HOME");
            }
            assert(chmod(ro, 0700) == 0);
            rmdir(ro);
        }
    } else {
        assert(!jfx_vk_used_gpu(backend));
    }
    jfx_vk_backend_destroy(backend);
    return 0;
}
