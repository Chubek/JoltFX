#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jfx/ffi_bridge.h"
#include "jfx/jfx_events.h"

#include "wasm_fixtures.h"
int main(void) {
    if (!jfx_script_language_available(JFX_SCRIPT_WASM)) return 0;
    tilly_allocator_t *allocator = tilly_allocator_create(TILLY_ALLOC_GENERAL, 8u * 1024u * 1024u);
    assert(allocator);
    jfx_script_desc_t desc = { .size = sizeof(desc), .allocator = allocator, .config = {1024u * 1024u, 10000} };
    jfx_script_runtime_t *rt = NULL;
    assert(jfx_script_runtime_create(JFX_SCRIPT_WASM, &desc, &rt) == JFX_SCRIPT_OK);
    size_t baseline = jfx_script_runtime_memory_used(rt);
    assert(jfx_script_runtime_load(rt, unsafe, sizeof(unsafe), "unsafe.wasm") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, "garbage", 7, "invalid.wasm") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, "(module)", 8, "text.wat") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, start_module, sizeof(start_module), "start.wasm") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, constructor_module, sizeof(constructor_module), "constructor.wasm") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, large_memory, sizeof(large_memory), "large.wasm") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, bad_abi, sizeof(bad_abi), "bad-abi.wasm") == JFX_SCRIPT_TYPE_ERROR);
    assert(jfx_script_runtime_memory_used(rt) == baseline);
    assert(jfx_script_runtime_load(rt, scalar_module, sizeof(scalar_module), "scalar.wasm") == JFX_SCRIPT_OK);
    double result = -1;
    assert(jfx_script_runtime_call_number(rt, "gain", 0.75, &result) == JFX_SCRIPT_OK && result == 1);
    jfx_value_t arg = {.type = JFX_TYPE_INT, .i = INT64_MAX}, out = {0};
    assert(jfx_script_runtime_call(rt, "integer", &arg, 1, &out) == JFX_SCRIPT_OK && out.i == arg.i);
    assert(jfx_script_runtime_call(rt, "spin", NULL, 0, &out) == JFX_SCRIPT_BUDGET);
    assert(jfx_script_runtime_call_number(rt, "gain", 0.25, &result) == JFX_SCRIPT_OK && result == 0.5);
    jfx_script_runtime_t *second = NULL;
    assert(jfx_script_runtime_create(JFX_SCRIPT_WASM, &desc, &second) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(second, scalar_module, sizeof(scalar_module), "second.wasm") == JFX_SCRIPT_OK);
    jfx_script_runtime_destroy(rt);
    assert(jfx_script_runtime_call_number(second, "gain", 0.25, &result) == JFX_SCRIPT_OK && result == 0.5);
    jfx_script_runtime_destroy(second);
    assert(jfx_script_runtime_create(JFX_SCRIPT_WASM, &desc, &rt) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, typed_module, sizeof(typed_module), "typed.wasm") == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, typed_module, sizeof(typed_module), "again.wasm") == JFX_SCRIPT_BUSY);
    out = arg;
    assert(jfx_script_runtime_call(rt, "bad_status", NULL, 0, &out) == JFX_SCRIPT_ERROR);
    assert(out.type == arg.type && out.i == arg.i);
    const jfx_value_t values[] = {
        { .type = JFX_TYPE_NIL }, { .type = JFX_TYPE_BOOL, .b = true },
        { .type = JFX_TYPE_INT, .i = INT64_MIN }, { .type = JFX_TYPE_FLOAT, .f = 1.23456789012345 },
        { .type = JFX_TYPE_STRING, .str = "Hello café" },
        { .type = JFX_TYPE_VEC4, .vec4 = { 1, 0.5, 0.25, 1 } },
        { .type = JFX_TYPE_COLOR, .color = UINT32_C(0x12345678) },
        { .type = JFX_TYPE_USERDATA, .userdata = allocator }
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        assert(jfx_script_runtime_call(rt, "identity", &values[i], 1, &out) == JFX_SCRIPT_OK);
        assert(out.type == values[i].type);
        if (out.type == JFX_TYPE_INT) assert(out.i == values[i].i);
        if (out.type == JFX_TYPE_FLOAT) assert(out.f == values[i].f);
        if (out.type == JFX_TYPE_STRING) assert(!strcmp(out.str, values[i].str));
        if (out.type == JFX_TYPE_VEC4) assert(!memcmp(out.vec4, values[i].vec4, sizeof(out.vec4)));
        if (out.type == JFX_TYPE_COLOR) assert(out.color == values[i].color);
        if (out.type == JFX_TYPE_USERDATA) assert(out.userdata == allocator);
    }
    jfx_engine_t *engine = NULL; jfx_engine_config_t config = { .max_worker_threads = 1 };
    assert(jfx_engine_init(&config, &engine) == JFX_SUCCESS);
    jfx_buffer_t *buffer = NULL;
    assert(jfx_buffer_create(engine, sizeof(float), JFX_BUFFER_USAGE_STORAGE, &buffer) == JFX_SUCCESS);
    float pixel = 0.25f; assert(jfx_buffer_write(buffer, 0, &pixel, sizeof(pixel)) == JFX_SUCCESS);
    jfx_value_t args[] = { { .type = JFX_TYPE_BUFFER, .buffer = buffer }, { .type = JFX_TYPE_INT, .i = 0 } };
    assert(jfx_script_runtime_call(rt, "read", args, 2, &out) == JFX_SCRIPT_OK && out.f == 0.25);
    assert(jfx_script_runtime_call(rt, "keep", args, 1, &out) == JFX_SCRIPT_OK);
    jfx_buffer_destroy(buffer);
    assert(jfx_script_runtime_call(rt, "expired", NULL, 0, &out) == JFX_SCRIPT_TYPE_ERROR);
    assert(jfx_script_runtime_call(rt, "forge", NULL, 0, &out) == JFX_SCRIPT_TYPE_ERROR);
    assert(jfx_script_runtime_call(rt, "grow", NULL, 0, &out) == JFX_SCRIPT_OK && out.type == JFX_TYPE_INT && out.i == -1);
    assert(jfx_script_runtime_memory_used(rt) <= desc.config.memory_limit);
    jfx_script_runtime_destroy(rt); jfx_engine_shutdown(engine);
    assert(allocator->alloc_count == allocator->free_count);
    tilly_allocator_destroy(allocator);
    puts("WAMR extension conformance passed");
    return 0;
}
