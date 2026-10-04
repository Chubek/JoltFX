#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jfx/ffi_bridge.h"
#include "jfx/jfx_events.h"

static const char *const scripts[] = {
    "function identity(x) return x end\nfunction batch(b,t) jfx.write(b,1,jfx.read(b,0)*2); jfx.write_pixel(t,1,0,{1,0.5,0,1}); return jfx.sample(t,1,0) end\n"
    "function bounds(b) return jfx.read(b,999) end\nfunction keep(b) saved=b end\nfunction expired() return jfx.size(saved) end\n"
    "function original(x) return x+1 end\nfunction denied() return jfx.command('track.add',0,0,0,0,'deny') end\n"
    "function event(e) error('event failed') end\nfunction fail_load() end\n",
    "def identity(x); x; end\ndef batch(b,t); JFX.write(b,1,JFX.read(b,0)*2); JFX.write_pixel(t,1,0,[1,0.5,0,1]); JFX.sample(t,1,0); end\n"
    "def bounds(b); JFX.read(b,999); end\ndef keep(b); $saved=b; nil; end\ndef expired; JFX.size($saved); end\n"
    "def original(x); x+1; end\ndef denied; JFX.command('track.add',0,0,0,0,'deny'); end\ndef event(e); raise 'event failed'; end\n",
    "function identity(x){return x;}function batch(b,t){jfx.write(b,1,jfx.read(b,0)*2);jfx.write_pixel(t,1,0,[1,0.5,0,1]);return jfx.sample(t,1,0);}"
    "function bounds(b){return jfx.read(b,999);} let saved; function keep(b){saved=b;}function expired(){return jfx.size(saved);}"
    "function original(x){return x+1;}function denied(){return jfx.command('track.add',0,0,0,0,'deny');}function event(e){throw Error('event failed');}",
    "import jfx\ndef identity(x):\n return x\ndef batch(b,t):\n jfx.write(b,1,jfx.read(b,0)*2)\n jfx.write_pixel(t,1,0,[1,0.5,0,1])\n return jfx.sample(t,1,0)\n"
    "def bounds(b):\n return jfx.read(b,999)\ndef keep(b):\n global saved\n saved=b\ndef expired():\n return jfx.size(saved)\n"
    "def original(x):\n return x+1\ndef denied():\n return jfx.command('track.add',0,0,0,0,'deny')\ndef event(e):\n raise RuntimeError('event failed')\n"
};
static const char *const replacements[] = {
    "function original(x) return x+100 end", "def original(x); x+100; end\n$_gc_root_ = []\n",
    "function original(x){return x+100;}", "def original(x):\n return x+100\n"
};
static const char *const callbacks[] = {
    "stopped=0; function once(e) stopped=stopped+1; jfx.off(subscription) end\n"
    "function start() subscription=jfx.on('frame_end',once) end\nfunction stopped_count() return stopped end\n"
    "function zero_off() return jfx.off(0) end\nfunction closure() return function(x) return x+2 end end\nfunction apply(f) return f(3) end\n",
    "$stopped=0\ndef once(e); $stopped+=1; JFX.off($subscription); end\n"
    "def start; $subscription=JFX.on('frame_end','once'); nil; end\ndef stopped_count; $stopped; end\n"
    "def zero_off; JFX.off(0); end\ndef closure; ->(x) { x+2 }; end\ndef apply(f); f.call(3); end\n",
    "let stopped=0, subscription; function once(e){stopped++;jfx.off(subscription);}"
    "function start(){subscription=jfx.on('frame_end',once);}function stopped_count(){return BigInt(stopped);}"
    "function zero_off(){return jfx.off(0);}function closure(){return x=>x+2;}function apply(f){return f(3);}",
    "stopped=0\ndef once(e):\n global stopped\n stopped+=1\n jfx.off(subscription)\n"
    "def start():\n global subscription\n subscription=jfx.on('frame_end','once')\ndef stopped_count():\n return stopped\n"
    "def zero_off():\n return jfx.off(0)\ndef closure():\n return lambda x: x+2\ndef apply(f):\n return f(3)\n"
};
static const char *const bad_loads[] = {
    "jfx.on('frame_end',event); jfx.register_kernel('rollback',original); error('rollback')",
    "JFX.on('frame_end','event'); JFX.register_kernel('rollback','original'); raise 'rollback'",
    "jfx.on('frame_end',event); jfx.register_kernel('rollback',original); throw Error('rollback');",
    "jfx.on('frame_end',event)\njfx.register_kernel('rollback',original)\nraise RuntimeError('rollback')\n"
};
static void check(jfx_script_language_t language, jfx_engine_t *engine) {
    tilly_allocator_t *allocator = tilly_allocator_create(TILLY_ALLOC_GENERAL, 8u * 1024u * 1024u);
    assert(allocator);
    jfx_script_desc_t desc = { .size = sizeof(desc), .allocator = allocator,
        .config = { 2u * 1024u * 1024u, 20000 }, .capabilities = JFX_SCRIPT_CAP_EVENTS };
    jfx_script_runtime_t *rt = NULL, *other = NULL;
    assert(jfx_script_runtime_create(language, &desc, &rt) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_create(language, &desc, &other) == JFX_SCRIPT_OK);
    const char *source = scripts[language];
    assert(jfx_script_runtime_load(rt, source, strlen(source), "resources") == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(other, source, strlen(source), "independent") == JFX_SCRIPT_OK);
    jfx_value_t scalar = { .type = JFX_TYPE_FLOAT, .f = 0.25 }, result = {0};
    jfx_script_value_t reference = 0;
    assert(jfx_script_runtime_from_native(rt, &scalar, &reference) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_to_native(rt, reference, &result) == JFX_SCRIPT_OK && result.f == scalar.f);
    assert(jfx_script_runtime_to_native(other, reference, &result) == JFX_SCRIPT_TYPE_ERROR);
    jfx_script_runtime_release_value(rt, reference);
    assert(jfx_script_runtime_to_native(rt, reference, &result) == JFX_SCRIPT_TYPE_ERROR);
    const jfx_value_t nil = { .type = JFX_TYPE_NIL };
    assert(jfx_script_runtime_from_native(rt, &nil, &reference) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_to_native(rt, reference, &result) == JFX_SCRIPT_OK && result.type == JFX_TYPE_NIL);
    jfx_script_runtime_release_value(rt, reference);
    assert(jfx_script_runtime_register_kernel(rt, "cached", "original") == JFX_SCRIPT_OK);
    source = replacements[language];
    assert(jfx_script_runtime_load(rt, source, strlen(source), "replace") == JFX_SCRIPT_OK);
    jfx_script_runtime_gc_collect(rt);
    assert(jfx_script_runtime_invoke_kernel(rt, "cached", &scalar, 1, &result) == JFX_SCRIPT_OK);
    assert(result.type == JFX_TYPE_FLOAT && result.f == 1.25);
    assert(jfx_script_runtime_call(other, "original", &scalar, 1, &result) == JFX_SCRIPT_OK && result.f == 1.25);
    assert(jfx_script_runtime_call(rt, "denied", NULL, 0, &result) == JFX_SCRIPT_CAPABILITY);

    jfx_buffer_t *buffer = NULL; jfx_texture_t *texture = NULL;
    assert(jfx_buffer_create(engine, 2 * sizeof(float), JFX_BUFFER_USAGE_STORAGE, &buffer) == JFX_SUCCESS);
    assert(jfx_texture_create(engine, 2, 1, JFX_FORMAT_R32G32B32A32_SFLOAT, &texture) == JFX_SUCCESS);
    float initial[] = { 0.25f, -1 };
    assert(jfx_buffer_write(buffer, 0, initial, sizeof(initial)) == JFX_SUCCESS);
    jfx_value_t args[] = { { .type = JFX_TYPE_BUFFER, .buffer = buffer }, { .type = JFX_TYPE_TEXTURE, .texture = texture } };
    assert(jfx_script_runtime_call(rt, "batch", args, 2, &result) == JFX_SCRIPT_OK);
    assert(result.type == JFX_TYPE_VEC4 && result.vec4[0] == 1 && result.vec4[1] == 0.5f && result.vec4[3] == 1);
    assert(jfx_buffer_read(buffer, 0, initial, sizeof(initial)) == JFX_SUCCESS && initial[1] == 0.5f);
    assert(jfx_script_runtime_call(rt, "identity", &args[1], 1, &result) == JFX_SCRIPT_OK && result.texture == texture);
    assert(jfx_script_runtime_call(rt, "bounds", args, 1, &result) != JFX_SCRIPT_OK);
    assert(strstr(jfx_script_runtime_last_error(rt), "bounds"));
    assert(jfx_script_runtime_call(rt, "keep", args, 1, &result) == JFX_SCRIPT_OK);
    jfx_buffer_destroy(buffer); jfx_texture_destroy(texture);
    assert(jfx_script_runtime_call(rt, "expired", NULL, 0, &result) == JFX_SCRIPT_TYPE_ERROR);
    assert(strstr(jfx_script_runtime_last_error(rt), "expired"));

    uint32_t count = event_subscriber_count(JFX_EVENT_FRAME_END), subscription = 0;
    source = bad_loads[language];
    assert(jfx_script_runtime_load(rt, source, strlen(source), "failed-load") != JFX_SCRIPT_OK);
    assert(event_subscriber_count(JFX_EVENT_FRAME_END) == count);
    assert(jfx_script_runtime_invoke_kernel(rt, "rollback", &scalar, 1, &result) == JFX_SCRIPT_NOT_FOUND);
    assert(jfx_script_runtime_on(rt, "frame_end", "event", &subscription) == JFX_SCRIPT_OK);
    event_publish(JFX_EVENT_FRAME_END, NULL);
    assert(jfx_script_runtime_last_status(rt) != JFX_SCRIPT_OK);
    assert(strstr(jfx_script_runtime_last_error(rt), "event failed"));
    assert(jfx_script_runtime_off(rt, subscription) == JFX_SCRIPT_OK);
    assert(event_subscriber_count(JFX_EVENT_FRAME_END) == count);
    assert(jfx_script_runtime_off(rt, subscription) == JFX_SCRIPT_NOT_FOUND);
    source = callbacks[language];
    assert(jfx_script_runtime_load(rt, source, strlen(source), "callbacks") == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_call(rt, "start", NULL, 0, &result) == JFX_SCRIPT_OK);
    assert(event_subscriber_count(JFX_EVENT_FRAME_END) == count + 1);
    event_publish(JFX_EVENT_FRAME_END, NULL);
    assert(jfx_script_runtime_last_status(rt) == JFX_SCRIPT_OK);
    assert(event_subscriber_count(JFX_EVENT_FRAME_END) == count);
    event_publish(JFX_EVENT_FRAME_END, NULL);
    assert(jfx_script_runtime_call(rt, "stopped_count", NULL, 0, &result) == JFX_SCRIPT_OK && result.i == 1);
    assert(jfx_script_runtime_call(rt, "zero_off", NULL, 0, &result) == JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_script_runtime_call(rt, "closure", NULL, 0, &result) == JFX_SCRIPT_OK && result.type == JFX_TYPE_FUNCTION);
    jfx_value_t function = result;
    jfx_script_runtime_gc_collect(rt);
    assert(jfx_script_runtime_call(rt, "apply", &function, 1, &result) == JFX_SCRIPT_OK);
    assert((result.type == JFX_TYPE_INT && result.i == 5) || (result.type == JFX_TYPE_FLOAT && result.f == 5));
    jfx_script_runtime_release_value(rt, function.function);
    jfx_script_runtime_destroy(other); jfx_script_runtime_destroy(rt);
    assert(allocator->alloc_count == allocator->free_count);
    tilly_allocator_destroy(allocator);
    for (size_t budget = 16u * 1024u; budget <= 256u * 1024u; budget *= 2) {
        desc.allocator = NULL; desc.config.memory_limit = budget;
        rt = (jfx_script_runtime_t *)(uintptr_t)1;
        jfx_script_status_t status = jfx_script_runtime_create(language, &desc, &rt);
        if (!status) jfx_script_runtime_destroy(rt); else assert(!rt);
    }
}
int main(void) {
    jfx_engine_config_t config = { .max_worker_threads = 1 }; jfx_engine_t *engine = NULL;
    assert(jfx_engine_init(&config, &engine) == JFX_SUCCESS);
    for (jfx_script_language_t language = JFX_SCRIPT_LUA; language <= JFX_SCRIPT_PYTHON; ++language)
        if (jfx_script_language_available(language)) check(language, engine);
    jfx_engine_shutdown(engine);
    puts("extension resource/lifetime conformance passed");
    return 0;
}
