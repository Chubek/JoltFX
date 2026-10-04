#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "jfx/ffi_bridge.h"

static const char *const sources[] = {
    "function gain(x) return jfx.clamp(x*2,0,1) end",
    "def gain(x); JFX.clamp(x*2,0,1); end",
    "function gain(x){return jfx.clamp(x*2,0,1);}",
    "import jfx\ndef gain(x):\n return jfx.clamp(x*2,0,1)\n",
    "(module (func (export \"gain\") (param f64) (result f64) local.get 0 f64.const 2 f64.mul))"
};
int main(void) {
    for (jfx_script_language_t language = JFX_SCRIPT_LUA; language < JFX_SCRIPT_LANGUAGE_COUNT; ++language) {
        if (!jfx_script_language_available(language)) continue;
        clock_t start = clock(); jfx_script_runtime_t *rt = NULL;
        jfx_script_status_t status = jfx_script_runtime_create(language, NULL, &rt);
        double init_ms = (double)(clock() - start) * 1000 / CLOCKS_PER_SEC;
        size_t initial_memory = jfx_script_runtime_memory_used(rt);
        start = clock();
        if (!status) status = jfx_script_runtime_load(rt, sources[language], strlen(sources[language]), "benchmark");
        if (!status) status = jfx_script_runtime_register_kernel(rt, "cached", "gain");
        double load_ms = (double)(clock() - start) * 1000 / CLOCKS_PER_SEC;
        jfx_value_t input = { .type = JFX_TYPE_FLOAT, .f = 0.25 }, result = {0};
        start = clock();
        const size_t iterations = 10000;
        for (size_t i = 0; i < iterations && !status; ++i) {
            status = jfx_script_runtime_invoke_kernel(rt, "cached", &input, 1, &result);
            if (!status && (result.type != JFX_TYPE_FLOAT || fabs(result.f - 0.5) > 1e-12)) status = JFX_SCRIPT_ERROR;
        }
        double call_us = (double)(clock() - start) * 1000000 / CLOCKS_PER_SEC / (double)iterations;
        if (status) fprintf(stderr, "%s: %s (%d)\n", jfx_script_language_name(language), jfx_script_runtime_last_error(rt), status);
        else printf("%s: init %.3f ms, initial %zu B, load+capture %.3f ms, cached call %.3f us (%zu calls)\n",
            jfx_script_language_name(language), init_ms, initial_memory, load_ms, call_us, iterations);
        jfx_script_runtime_destroy(rt);
        if (status) return 1;
    }
    return 0;
}
