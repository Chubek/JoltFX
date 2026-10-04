#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jfx/ffi_bridge.h"
#include "jfx/jfx_buffer.h"
#include "jfx/jfx_texture.h"
#include "jfx/jfx_events.h"

static void run(jfx_script_language_t language, const char *source,
    const char *unsafe_source, const char *budget_source, const char *memory_source) {
    jfx_editor_t *editor = jfx_editor_create(2, 2);
    assert(editor);
    jfx_script_desc_t desc = {0};
    desc.size = sizeof(desc);
    desc.config.memory_limit = 2u * 1024u * 1024u;
    desc.config.instruction_limit = 20000;
    desc.editor = editor;
    desc.capabilities = JFX_SCRIPT_CAP_EDITOR | JFX_SCRIPT_CAP_EVENTS;
    jfx_script_runtime_t *rt = NULL;
    assert(jfx_script_runtime_create(language, &desc, &rt) == JFX_SCRIPT_OK && rt);
    assert(jfx_script_runtime_load(rt, source, strlen(source), "conformance") == JFX_SCRIPT_OK);

    jfx_value_t input = { .type = JFX_TYPE_FLOAT, .f = 0.75 }, result = {0};
    assert(jfx_script_runtime_call(rt, "gain", &input, 1, &result) == JFX_SCRIPT_OK);
    assert(result.type == JFX_TYPE_FLOAT && fabs(result.f - 1.0) < 1e-12);
    const jfx_value_t values[] = {
        { .type = JFX_TYPE_NIL }, { .type = JFX_TYPE_BOOL, .b = true },
        { .type = JFX_TYPE_INT, .i = INT64_C(9007199254740993) },
        { .type = JFX_TYPE_INT, .i = INT64_MIN },
        { .type = JFX_TYPE_FLOAT, .f = 1.23456789012345 },
        { .type = JFX_TYPE_STRING, .str = "UTF-8: café" },
        { .type = JFX_TYPE_VEC2, .vec2 = {1, 2} },
        { .type = JFX_TYPE_VEC3, .vec3 = {1, 2, 3} },
        { .type = JFX_TYPE_VEC4, .vec4 = {1, 2, 3, 4} },
        { .type = JFX_TYPE_COLOR, .color = UINT32_C(0xff804020) },
        { .type = JFX_TYPE_USERDATA, .userdata = editor }
    };
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        assert(jfx_script_runtime_call(rt, "identity", &values[i], 1, &result) == JFX_SCRIPT_OK);
        assert(result.type == values[i].type);
        switch (result.type) {
            case JFX_TYPE_INT: assert(result.i == values[i].i); break;
            case JFX_TYPE_BOOL: assert(result.b == values[i].b); break;
            case JFX_TYPE_FLOAT: assert(result.f == values[i].f); break;
            case JFX_TYPE_STRING: assert(!strcmp(result.str, values[i].str)); break;
            case JFX_TYPE_COLOR: assert(result.color == values[i].color); break;
            case JFX_TYPE_USERDATA: assert(result.userdata == editor); break;
            case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4:
                assert(!memcmp(result.vec4, values[i].vec4,
                    (size_t)(result.type - JFX_TYPE_VEC2 + 2) * sizeof(float))); break;
            default: break;
        }
    }
    result = input;
    assert(jfx_script_runtime_call(rt, "missing", NULL, 0, &result) == JFX_SCRIPT_NOT_FOUND);
    assert(result.type == input.type && result.f == input.f);
    assert(jfx_script_runtime_call(rt, "bad_type", NULL, 0, &result) == JFX_SCRIPT_TYPE_ERROR);
    assert(strstr(jfx_script_runtime_last_error(rt), "clamp"));
    assert(jfx_script_runtime_load(rt, unsafe_source, strlen(unsafe_source), "sandbox") != JFX_SCRIPT_OK);
    assert(*jfx_script_runtime_last_error(rt));
    assert(jfx_script_runtime_call(rt, "edit", NULL, 0, &result) == JFX_SCRIPT_OK);
    assert(jfx_editor_can_undo(editor));

    const uint32_t before = event_subscriber_count(JFX_EVENT_FRAME_BEGIN);
    assert(jfx_script_runtime_call(rt, "subscribe", NULL, 0, &result) == JFX_SCRIPT_OK);
    assert(event_subscriber_count(JFX_EVENT_FRAME_BEGIN) == before + 1);
    event_publish(JFX_EVENT_FRAME_BEGIN, NULL);
    assert(jfx_script_runtime_call(rt, "event_count", NULL, 0, &result) == JFX_SCRIPT_OK);
    assert(result.type == JFX_TYPE_INT && result.i == 1);
    assert(jfx_script_runtime_load(rt, budget_source, strlen(budget_source), "budget") == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_call(rt, "spin", NULL, 0, &result) == JFX_SCRIPT_BUDGET);
    assert(jfx_script_runtime_call(rt, "recurse", NULL, 0, &result) != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_call(rt, "gain", &input, 1, &result) == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_load(rt, memory_source, strlen(memory_source), "memory") == JFX_SCRIPT_OK);
    assert(jfx_script_runtime_call(rt, "hungry", NULL, 0, &result) != JFX_SCRIPT_OK);
    assert(jfx_script_runtime_memory_used(rt) <= desc.config.memory_limit);
    jfx_script_runtime_gc_pause(rt);
    jfx_script_runtime_gc_collect(rt);
    jfx_script_runtime_gc_resume(rt);
    jfx_script_runtime_gc_collect(rt);
    assert(jfx_script_runtime_call(rt, "identity", &input, 1, &result) == JFX_SCRIPT_OK);
    jfx_script_runtime_destroy(rt);
    assert(event_subscriber_count(JFX_EVENT_FRAME_BEGIN) == before);
    jfx_editor_destroy(editor);
}

int main(void) {
    assert(event_bus_init());
    if (jfx_script_language_available(JFX_SCRIPT_LUA)) run(JFX_SCRIPT_LUA,
        "function identity(x) return x end\nfunction gain(x) return jfx.clamp(x*2,0,1) end\n"
        "function bad_type() return jfx.clamp('1',0,1) end\n"
        "function edit() return jfx.command('track.add',0,0,0,0,'Script') end\n"
        "count=0; function frame_event(e) count=count+1 end\n"
        "function subscribe() return jfx.on('frame_begin',frame_event) end\n"
        "function event_count() return count end\n",
        "io.open('/etc/passwd')", "function spin() while true do end end\nfunction recurse() return recurse() end",
        "function hungry() return string.rep('x',8000000) end");
    if (jfx_script_language_available(JFX_SCRIPT_MRUBY)) run(JFX_SCRIPT_MRUBY,
        "def identity(x); x; end\ndef gain(x); JFX.clamp(x*2,0,1); end\n"
        "def bad_type; JFX.clamp('1',0,1); end\n"
        "def edit; JFX.command('track.add',0,0,0,0,'Script'); end\n"
        "$count=0\ndef frame_event(e); $count+=1; end\n"
        "def subscribe; JFX.on('frame_begin','frame_event'); end\n"
        "def event_count; $count; end\n",
        "File.open('/etc/passwd')", "def spin; while true; end; end\ndef recurse; recurse; end",
        "def hungry; 'x' * 8000000; end");
    if (jfx_script_language_available(JFX_SCRIPT_QUICKJS)) run(JFX_SCRIPT_QUICKJS,
        "function identity(x){return x;} function gain(x){return jfx.clamp(x*2,0,1);}"
        "function bad_type(){return jfx.clamp('1',0,1);}"
        "function edit(){return jfx.command('track.add',0,0,0,0,'Script');}"
        "let count=0; function frame_event(e){count++;}"
        "function subscribe(){return jfx.on('frame_begin',frame_event);}"
        "function event_count(){return BigInt(count);}",
        "std.open('/etc/passwd')", "function spin(){while(true){}}function recurse(){return recurse();}",
        "function hungry(){return 'x'.repeat(8000000);}");
    if (jfx_script_language_available(JFX_SCRIPT_PYTHON)) run(JFX_SCRIPT_PYTHON,
        "import jfx\ndef identity(x):\n return x\ndef gain(x):\n return jfx.clamp(x*2,0,1)\n"
        "def bad_type():\n return jfx.clamp('1',0,1)\n"
        "def edit():\n return jfx.command('track.add',0,0,0,0,'Script')\n"
        "count=0\ndef frame_event(e):\n global count\n count+=1\n"
        "def subscribe():\n return jfx.on('frame_begin',frame_event)\n"
        "def event_count():\n return count\n",
        "open('/etc/passwd')", "def spin():\n while True:\n  pass\ndef recurse():\n return recurse()\n",
        "def hungry():\n return 'x' * 8000000\n");
    assert(jfx_script_runtime_call(NULL, "identity", NULL, 0, NULL) == JFX_SCRIPT_INVALID_ARGUMENT);
    assert(jfx_script_runtime_load(NULL, "", 0, "empty") == JFX_SCRIPT_INVALID_ARGUMENT);
    jfx_script_runtime_destroy(NULL);
    event_bus_shutdown();
    puts("extension bridge conformance passed");
    return 0;
}
