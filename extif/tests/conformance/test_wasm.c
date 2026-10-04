#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jfx/ffi_bridge.h"
#include "jfx/jfx_events.h"

static const char scalar_module[] =
    "(module (import \"joltfx\" \"clamp\" (func $clamp (param f64 f64 f64) (result f64)))"
    "(func (export \"gain\") (param f64) (result f64) local.get 0 f64.const 2 f64.mul f64.const 0 f64.const 1 call $clamp)"
    "(func (export \"integer\") (param i64) (result i64) local.get 0)"
    "(func (export \"spin\") (loop $loop br $loop)))";
static const char typed_module[] =
    "(module (import \"joltfx\" \"call\" (func $call (param i32 i32 i32 i32 i32) (result i32)))"
    "(memory (export \"memory\") 1)"
    "(global (export \"jfx_abi_version\") i32 (i32.const 1))"
    "(global (export \"jfx_scratch\") i32 (i32.const 4096))"
    "(global (export \"jfx_scratch_size\") i32 (i32.const 4096))"
    "(data (i32.const 0) \"read\") (data (i32.const 16) \"size\")"
    "(func $identity (export \"identity\") (param $args i32) (param $n i32) (param $out i32) (result i32)"
    "local.get $out local.get $args i64.load i64.store "
    "local.get $out local.get $args i64.load offset=8 i64.store offset=8 "
    "local.get $out local.get $args i64.load offset=16 i64.store offset=16 "
    "local.get $out local.get $args i64.load offset=24 i64.store offset=24 i32.const 0)"
    "(func (export \"read\") (param $args i32) (param $n i32) (param $out i32) (result i32)"
    "i32.const 0 i32.const 4 local.get $args local.get $n local.get $out call $call)"
    "(func (export \"keep\") (param i32 i32 i32) (result i32) local.get 0 local.get 1 i32.const 1024 call $identity)"
    "(func (export \"expired\") (param i32 i32 i32) (result i32) i32.const 16 i32.const 4 i32.const 1024 i32.const 1 local.get 2 call $call)"
    "(func (export \"forge\") (param i32 i32 i32) (result i32) i32.const 8 i32.const 99999 i32.store "
    "i32.const 0 i32.const 4 i32.const 65530 i32.const 2 local.get 2 call $call)"
    "(func (export \"grow\") (param i32 i32 i32) (result i32) (local $grown i32) "
    "i32.const 1024 memory.grow local.set $grown local.get 2 i32.const 2 i32.store "
    "local.get 2 local.get $grown i64.extend_i32_s i64.store offset=8 i32.const 0)"
    "(func (export \"bad_status\") (param i32 i32 i32) (result i32) i32.const 100))";
int main(void) {
    if (!jfx_script_language_available(JFX_SCRIPT_WASM)) return 0;
    tilly_allocator_t *allocator = tilly_allocator_create(TILLY_ALLOC_GENERAL, 8u * 1024u * 1024u);
    assert(allocator);
    jfx_script_desc_t desc = { .size = sizeof(desc), .allocator = allocator, .config = {1024u * 1024u, 10000} };
    jfx_script_runtime_t *rt = NULL;
    assert(jfx_script_runtime_create(JFX_SCRIPT_WASM, &desc, &rt) == JFX_SCRIPT_OK);
    const char unsafe[] = "(module (import \"wasi_snapshot_preview1\" \"fd_write\" (func)))";
    assert(jfx_script_runtime_load(rt, unsafe, sizeof(unsafe) - 1, "unsafe.wat") != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, "garbage", 7, "invalid.wasm") != JFX_SCRIPT_OK);
    const char bad_abi[] = "(module (global (export \"jfx_abi_version\") f64 (f64.const 1)))";
    assert(jfx_script_runtime_load(rt, bad_abi, sizeof(bad_abi) - 1, "bad-abi.wat") == JFX_SCRIPT_TYPE_ERROR);
    assert(jfx_script_runtime_load(rt, scalar_module, sizeof(scalar_module) - 1, "scalar.wat") == JFX_SCRIPT_OK);
    double result = -1;
    assert(jfx_script_runtime_call_number(rt, "gain", 0.75, &result) == JFX_SCRIPT_OK && result == 1);
    jfx_value_t arg = {.type = JFX_TYPE_INT, .i = INT64_MAX}, out = {0};
    assert(jfx_script_runtime_call(rt, "integer", &arg, 1, &out) == JFX_SCRIPT_OK && out.i == arg.i);
    assert(jfx_script_runtime_call(rt, "spin", NULL, 0, &out) == JFX_SCRIPT_BUDGET);
    assert(jfx_script_runtime_call_number(rt, "gain", 0.25, &result) == JFX_SCRIPT_OK && result == 0.5);
    jfx_script_runtime_destroy(rt);
    assert(jfx_script_runtime_create(JFX_SCRIPT_WASM, &desc, &rt) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, typed_module, sizeof(typed_module) - 1, "typed.wat") == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, typed_module, sizeof(typed_module) - 1, "again.wat") == JFX_SCRIPT_BUSY);
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
    puts("Wasmtime extension conformance passed");
    return 0;
}
