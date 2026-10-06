#include <stdio.h>
#include <string.h>
#include "jfx/ffi_bridge.h"
#include "gain_wasm.h"

static const char *const sources[] = {
    "function gain(x) return jfx.clamp(x*2,0,1) end",
    "def gain(x); JFX.clamp(x*2,0,1); end",
    "function gain(x){return jfx.clamp(x*2,0,1);}",
    "import pyjoltfx as jfx\ndef gain(x):\n return jfx.clamp(x*2,0,1)\n",
    NULL
};
int main(void) {
    const jfx_ffi_bridge_t *bridge = jfx_script_ffi_bridge();
    for (jfx_script_language_t language = JFX_SCRIPT_LUA; language < JFX_SCRIPT_LANGUAGE_COUNT; ++language) {
        if (!jfx_script_language_available(language)) continue;
        jfx_script_runtime_t *runtime = NULL;
        jfx_script_status_t status = bridge->init(language, NULL, &runtime);
        if (!status) status = language == JFX_SCRIPT_WASM ?
            bridge->load_script(runtime, gain_wasm, sizeof(gain_wasm), "embedded.wasm") :
            bridge->load_script(runtime, sources[language], strlen(sources[language]), "embedded");
        const jfx_value_t input = { .type = JFX_TYPE_FLOAT, .f = 0.25 };
        jfx_value_t result = {0};
        if (!status) status = bridge->call_function(runtime, "gain", &input, 1, &result);
        if (!status && (result.type != JFX_TYPE_FLOAT || result.f != 0.5)) status = JFX_SCRIPT_TYPE_ERROR;
        const jfx_value_t text = { .type = JFX_TYPE_STRING, .str = "Hello JoltFX" };
        jfx_script_value_t reference = 0;
        if (!status) status = bridge->from_native(runtime, &text, &reference);
        if (!status) status = bridge->to_native(runtime, reference, &result);
        if (!status && (result.type != JFX_TYPE_STRING || strcmp(result.str, text.str))) status = JFX_SCRIPT_TYPE_ERROR;
        jfx_script_runtime_release_value(runtime, reference);
        if (status) fprintf(stderr, "%s: %s (%d)\n", jfx_script_language_name(language), jfx_script_runtime_last_error(runtime), status);
        else printf("%s: gain=0.5, typed string round-trip passed\n", jfx_script_language_name(language));
        bridge->shutdown(runtime);
        if (status) return 1;
    }
    return 0;
}
