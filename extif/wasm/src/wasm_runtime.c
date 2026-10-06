#include "jfx/wasm.h"
#include "runtime_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stddef.h>
#include "wasm_export.h"

typedef struct {
    bool used, callable;
    uint32_t generation;
    wasm_function_inst_t function;
    jfx_value_t value;
    char *string;
} jfx_wasm_ref_t;
typedef struct {
    jfx_script_runtime_t base;
    wasm_module_t module;
    wasm_module_inst_t instance;
    wasm_exec_env_t exec_env;
    uint8_t *binary;
    bool loaded, typed;
    uint32_t scratch, scratch_size;
    jfx_wasm_ref_t refs[JFX_SCRIPT_REFS];
} wasm_runtime_t;
static wasm_runtime_t *as_wasm(jfx_script_runtime_t *rt) { return (wasm_runtime_t *)rt; }

/* WAMR has process-wide allocator callbacks. The Script API serializes lifecycle
 * on one owner thread; headers retain the allocating runtime even during frees
 * performed under a different active runtime. Bootstrap allocations are shared. */
static size_t runtime_count;
static _Thread_local jfx_script_runtime_t *allocation_owner;
typedef union {
    max_align_t alignment;
    struct { jfx_script_runtime_t *owner; size_t size; } info;
} allocation_t;
static void *block_alloc(unsigned int size) {
    size_t bytes = sizeof(allocation_t) + (size_t)size;
    if (bytes < size) return NULL;
    allocation_t *p = allocation_owner ? jfx_script_alloc(allocation_owner, NULL, bytes) :
        tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes, _Alignof(max_align_t));
    if (!p) return NULL;
    p->info.owner = allocation_owner; p->info.size = size; return p + 1;
}
static void block_free(void *ptr) {
    if (!ptr) return;
    allocation_t *p = (allocation_t *)ptr - 1;
    if (p->info.owner) (void)jfx_script_alloc(p->info.owner, p, 0);
    else tilly_free((tilly_allocator_t *)tilly_default_allocator(), p);
}
static void *block_realloc(void *ptr, unsigned int size) {
    if (!ptr) return block_alloc(size);
    if (!size) { block_free(ptr); return NULL; }
    allocation_t *p = (allocation_t *)ptr - 1;
    size_t bytes = sizeof(*p) + (size_t)size;
    if (bytes < size) return NULL;
    if (p->info.owner) {
        allocation_t *next = jfx_script_alloc(p->info.owner, p, bytes);
        if (!next) return NULL;
        next->info.size = size; return next + 1;
    }
    void *next = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes, _Alignof(max_align_t));
    if (!next) return NULL;
    allocation_t *block = next; block->info.owner = NULL; block->info.size = size;
    memcpy(block + 1, ptr, p->info.size < size ? p->info.size : size);
    block_free(ptr); return block + 1;
}
static void *wamr_malloc(mem_alloc_usage_t usage, unsigned int size) {
    void *ptr = block_alloc(size);
    if (ptr && usage == Alloc_For_LinearMemory) memset(ptr, 0, size);
    return ptr;
}
static void *wamr_realloc(mem_alloc_usage_t usage, bool mapped, void *ptr, unsigned int size) {
    (void)mapped;
    size_t old = ptr ? ((allocation_t *)ptr - 1)->info.size : 0;
    void *next = block_realloc(ptr, size);
    if (next && usage == Alloc_For_LinearMemory && size > old)
        memset((uint8_t *)next + old, 0, size - old);
    return next;
}
static void wamr_free(mem_alloc_usage_t usage, void *ptr) {
    (void)usage; block_free(ptr);
}
static jfx_script_status_t error(jfx_script_runtime_t *rt, const char *text) {
    jfx_script_status_t status = rt->fault ? rt->fault : JFX_SCRIPT_ERROR;
    if (text && strstr(text, "instruction limit exceeded")) status = JFX_SCRIPT_BUDGET;
    if (text) (void)snprintf(rt->error, sizeof(rt->error), "%s", text);
    return status;
}
static bool memory(wasm_runtime_t *wasm, uint8_t **data, size_t *size) {
    wasm_memory_inst_t mem = wasm_runtime_lookup_memory(wasm->instance, "memory");
    if (!mem) return false;
    *data = wasm_memory_get_base_address(mem);
    uint64_t bytes = wasm_memory_get_cur_page_count(mem) * wasm_memory_get_bytes_per_page(mem);
    if (bytes > SIZE_MAX) return false;
    *size = (size_t)bytes; return true;
}
static int ref_slot(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    size_t slot = (size_t)(ref & 255u);
    if (!slot || slot > JFX_SCRIPT_REFS) return -1;
    jfx_wasm_ref_t *entry = &as_wasm(rt)->refs[slot - 1];
    return entry->used && jfx_script_ref_matches(rt, ref, entry->generation) ? (int)(slot - 1) : -1;
}
static jfx_script_status_t pin(jfx_script_runtime_t *rt, const jfx_wasm_ref_t *value, jfx_script_value_t *out) {
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) {
        jfx_wasm_ref_t *ref = &as_wasm(rt)->refs[i];
        if (ref->used) continue;
        uint32_t generation = ref->generation + 1; if (!generation) ++generation;
        *ref = *value; ref->used = true; ref->generation = generation; ref->string = NULL;
        if (!value->callable && value->value.type == JFX_TYPE_STRING) {
            size_t n = strlen(value->value.str) + 1;
            ref->string = jfx_script_alloc(rt, NULL, n);
            if (!ref->string) { ref->used = false; return JFX_SCRIPT_OUT_OF_MEMORY; }
            memcpy(ref->string, value->value.str, n); ref->value.str = ref->string;
        }
        *out = jfx_script_ref_token(rt, generation, i); return JFX_SCRIPT_OK;
    }
    return JFX_SCRIPT_OUT_OF_MEMORY;
}
static void release(jfx_script_runtime_t *rt, jfx_script_value_t ref) {
    int slot = ref_slot(rt, ref); if (slot < 0) return;
    jfx_wasm_ref_t *entry = &as_wasm(rt)->refs[slot];
    (void)jfx_script_alloc(rt, entry->string, 0); entry->string = NULL; entry->used = false;
}
static uint32_t read32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t read64(const uint8_t *p) { return read32(p) | (uint64_t)read32(p + 4) << 32; }
static void write32(uint8_t *p, uint32_t v) { for (size_t i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i)); }
static void write64(uint8_t *p, uint64_t v) { write32(p, (uint32_t)v); write32(p + 4, (uint32_t)(v >> 32)); }
static bool span(size_t size, uint32_t offset, size_t length) { return offset <= size && length <= size - offset; }
static jfx_script_status_t read_value(jfx_script_runtime_t *rt, uint8_t *data, size_t size,
    uint32_t offset, bool copy, char **borrowed, jfx_value_t *out) {
    if (!span(size, offset, JFX_WASM_VALUE_SIZE)) return JFX_SCRIPT_TYPE_ERROR;
    uint8_t *p = data + offset;
    *out = (jfx_value_t){ .type = (jfx_value_type_t)read32(p) };
    uint64_t bits = read64(p + 8);
    switch (out->type) {
        case JFX_TYPE_NIL: break;
        case JFX_TYPE_BOOL: if (bits > 1) return JFX_SCRIPT_TYPE_ERROR; out->b = bits != 0; break;
        case JFX_TYPE_INT: memcpy(&out->i, &bits, sizeof(bits)); break;
        case JFX_TYPE_FLOAT: memcpy(&out->f, &bits, sizeof(bits)); if (!isfinite(out->f)) return JFX_SCRIPT_TYPE_ERROR; break;
        case JFX_TYPE_COLOR: out->color = (uint32_t)bits; break;
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4:
            for (size_t i = 0; i < (size_t)(out->type - JFX_TYPE_VEC2 + 2); ++i) {
                uint32_t lane = read32(p + 8 + i * 4); memcpy(&out->vec4[i], &lane, sizeof(lane));
                if (!isfinite(out->vec4[i])) return JFX_SCRIPT_TYPE_ERROR;
            }
            break;
        case JFX_TYPE_STRING: {
            uint32_t begin = read32(p + 8), length = read32(p + 12);
            if (!span(size, begin, length) || memchr(data + begin, 0, length)) return JFX_SCRIPT_TYPE_ERROR;
            if (copy) return jfx_script_copy_string(rt, (const char *)data + begin, length, out);
            char *text = jfx_script_alloc(rt, NULL, (size_t)length + 1);
            if (!text) return JFX_SCRIPT_OUT_OF_MEMORY;
            memcpy(text, data + begin, length); text[length] = 0; *borrowed = text; out->str = text; break;
        }
        case JFX_TYPE_BUFFER: case JFX_TYPE_TEXTURE: case JFX_TYPE_USERDATA: {
            jfx_value_type_t type = out->type;
            jfx_script_status_t status = jfx_script_unwrap(rt, bits, out);
            if (status || out->type != type) return JFX_SCRIPT_TYPE_ERROR;
            break;
        }
        default: return JFX_SCRIPT_TYPE_ERROR;
    }
    return JFX_SCRIPT_OK;
}
static jfx_script_status_t write_value(jfx_script_runtime_t *rt, uint8_t *data, size_t size,
    uint32_t offset, const jfx_value_t *value, uint32_t *cursor, uint32_t end) {
    if (!span(size, offset, JFX_WASM_VALUE_SIZE)) return JFX_SCRIPT_TYPE_ERROR;
    uint8_t *p = data + offset; uint64_t bits = 0;
    uint32_t string_offset = read32(p + 8), string_capacity = read32(p + 12);
    memset(p, 0, JFX_WASM_VALUE_SIZE); write32(p, (uint32_t)value->type);
    switch (value->type) {
        case JFX_TYPE_NIL: break;
        case JFX_TYPE_BOOL: write64(p + 8, value->b); break;
        case JFX_TYPE_INT: memcpy(&bits, &value->i, sizeof(bits)); write64(p + 8, bits); break;
        case JFX_TYPE_FLOAT: memcpy(&bits, &value->f, sizeof(bits)); write64(p + 8, bits); break;
        case JFX_TYPE_COLOR: write32(p + 8, value->color); break;
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4:
            for (size_t i = 0; i < (size_t)(value->type - JFX_TYPE_VEC2 + 2); ++i) {
                uint32_t lane; memcpy(&lane, &value->vec4[i], sizeof(lane)); write32(p + 8 + i * 4, lane);
            }
            break;
        case JFX_TYPE_STRING: {
            size_t length = strlen(value->str);
            if (cursor) { string_offset = *cursor; string_capacity = end >= *cursor ? end - *cursor : 0; }
            if (length > string_capacity || !span(size, string_offset, length)) return JFX_SCRIPT_OUT_OF_MEMORY;
            memcpy(data + string_offset, value->str, length);
            write32(p + 8, string_offset); write32(p + 12, (uint32_t)length);
            if (cursor) *cursor += (uint32_t)length;
            break;
        }
        case JFX_TYPE_BUFFER: case JFX_TYPE_TEXTURE: case JFX_TYPE_USERDATA: {
            jfx_script_status_t status = jfx_script_wrap(rt, value, &bits); if (status) return status;
            write64(p + 8, bits); break;
        }
        default: return JFX_SCRIPT_TYPE_ERROR;
    }
    return JFX_SCRIPT_OK;
}
static void host_call(wasm_exec_env_t env, uint64_t *argv) {
    jfx_script_runtime_t *rt = wasm_runtime_get_custom_data(wasm_runtime_get_module_inst(env));
    uint8_t *data = NULL; size_t size = 0;
    jfx_script_status_t status = JFX_SCRIPT_TYPE_ERROR;
    if (memory(as_wasm(rt), &data, &size)) {
        uint32_t op = (uint32_t)argv[0], length = (uint32_t)argv[1];
        uint32_t args_offset = (uint32_t)argv[2], count = (uint32_t)argv[3];
        uint32_t result_offset = (uint32_t)argv[4];
        if (length < 96 && span(size, op, length) && !memchr(data + op, 0, length) &&
            count <= JFX_SCRIPT_MAX_ARGS && span(size, args_offset, (size_t)count * JFX_WASM_VALUE_SIZE) &&
            span(size, result_offset, JFX_WASM_VALUE_SIZE)) {
            char name[96]; memcpy(name, data + op, length); name[length] = 0;
            jfx_value_t args[JFX_SCRIPT_MAX_ARGS] = {{0}}, result = {0};
            char *strings[JFX_SCRIPT_MAX_ARGS] = {0}; status = JFX_SCRIPT_OK;
            for (uint32_t i = 0; i < count; ++i) {
                status = read_value(rt, data, size, args_offset + i * JFX_WASM_VALUE_SIZE, false, &strings[i], &args[i]);
                if (status) break;
            }
            if (!status) status = jfx_script_host_call(rt, name, args, count, &result);
            if (!status && !memory(as_wasm(rt), &data, &size)) status = JFX_SCRIPT_TYPE_ERROR;
            if (!status) status = write_value(rt, data, size, result_offset, &result, NULL, 0);
            for (uint32_t i = 0; i < count; ++i) (void)jfx_script_alloc(rt, strings[i], 0);
        }
    }
    argv[0] = (uint32_t)status;
    if (status) (void)jfx_script_fail(rt, status, "joltwasm host call failed");
}
static void host_clamp(wasm_exec_env_t env, uint64_t *args) {
    wasm_module_inst_t instance = wasm_runtime_get_module_inst(env);
    jfx_script_runtime_t *rt = wasm_runtime_get_custom_data(instance);
    jfx_value_t values[3], result = {0};
    for (size_t i = 0; i < 3; ++i) {
        values[i] = (jfx_value_t){ .type = JFX_TYPE_FLOAT };
        memcpy(&values[i].f, &args[i], sizeof(double));
    }
    jfx_script_status_t status = jfx_script_host_call(rt, "clamp", values, 3, &result);
    if (status) {
        (void)jfx_script_fail(rt, status, "joltwasm clamp failed");
        wasm_runtime_set_exception(instance, rt->error); return;
    }
    memcpy(args, &result.f, sizeof(double));
}
static bool global_i32(wasm_runtime_t *wasm, const char *name, uint32_t *out) {
    wasm_global_inst_t global;
    if (!wasm_runtime_get_export_global_inst(wasm->instance, name, &global) ||
        global.kind != WASM_I32 || global.is_mutable) return false;
    int32_t value; memcpy(&value, global.global_data, sizeof(value));
    if (value < 0) return false;
    *out = (uint32_t)value; return true;
}
static void unload(wasm_runtime_t *wasm) {
    if (wasm->exec_env) wasm_runtime_destroy_exec_env(wasm->exec_env);
    if (wasm->instance) wasm_runtime_deinstantiate(wasm->instance);
    if (wasm->module) wasm_runtime_unload(wasm->module);
    (void)jfx_script_alloc(&wasm->base, wasm->binary, 0);
    wasm->exec_env = NULL; wasm->instance = NULL; wasm->module = NULL; wasm->binary = NULL;
    wasm->loaded = false; wasm->typed = false;
}
/* Instantiation runs start/constructor functions before an exec_env can be
 * metered. Reject those entry points rather than execute unbounded guest code. */
static bool no_start(const uint8_t *bytes, size_t length) {
    for (size_t offset = 8; offset < length;) {
        uint8_t id = bytes[offset++]; uint32_t size = 0; bool complete = false;
        for (unsigned shift = 0; shift < 35 && offset < length; shift += 7) {
            uint8_t byte = bytes[offset++];
            if (shift == 28 && (byte & 0xf0)) return false;
            size |= (uint32_t)(byte & 0x7f) << shift;
            if (!(byte & 0x80)) { complete = true; break; }
        }
        if (!complete || size > length - offset || id == 8) return false;
        offset += size;
    }
    return true;
}
static jfx_script_status_t load(jfx_script_runtime_t *rt, const void *source, size_t length, const char *name) {
    (void)name;
    wasm_runtime_t *wasm = as_wasm(rt);
    if (wasm->loaded) return JFX_SCRIPT_BUSY;
    if (length < 8 || length > UINT32_MAX || memcmp(source, "\0asm\1\0\0\0", 8))
        return error(rt, "WAMR requires a binary .wasm module; compile WAT with wat2wasm first");
    if (!no_start(source, length)) return error(rt, "WASM start sections are not supported");
    wasm->binary = jfx_script_alloc(rt, NULL, length);
    if (!wasm->binary) return JFX_SCRIPT_OUT_OF_MEMORY;
    memcpy(wasm->binary, source, length);
    jfx_script_runtime_t *previous = allocation_owner; allocation_owner = rt;
    wasm->module = wasm_runtime_load(wasm->binary, (uint32_t)length, rt->error, sizeof(rt->error));
    jfx_script_status_t status = wasm->module ? JFX_SCRIPT_OK : error(rt, NULL);
    for (int32_t i = 0; !status && i < wasm_runtime_get_import_count(wasm->module); ++i) {
        wasm_import_t item; wasm_runtime_get_import_type(wasm->module, i, &item);
        if (item.kind != WASM_IMPORT_EXPORT_KIND_FUNC || !item.linked ||
            strcmp(item.module_name, "joltfx") || (strcmp(item.name, "call") && strcmp(item.name, "clamp")))
            status = error(rt, "Unsupported WASM import; only joltfx.call and joltfx.clamp are available");
    }
    for (int32_t i = 0; !status && i < wasm_runtime_get_export_count(wasm->module); ++i) {
        wasm_export_t item; wasm_runtime_get_export_type(wasm->module, i, &item);
        if (!strcmp(item.name, "__post_instantiate") || !strcmp(item.name, "__wasm_call_ctors"))
            status = error(rt, "WASM instantiation constructors are not supported; call an explicit export");
    }
    if (!status) {
        struct InstantiationArgs2 *config = NULL;
        if (!wasm_runtime_instantiation_args_create(&config)) status = JFX_SCRIPT_OUT_OF_MEMORY;
        else {
            wasm_runtime_instantiation_args_set_default_stack_size(config, 16384);
            wasm_runtime_instantiation_args_set_max_memory_pages(config,
                (uint32_t)(rt->memory_limit / 65536 > 65536 ? 65536 : rt->memory_limit / 65536));
            wasm_runtime_instantiation_args_set_custom_data(config, rt);
            wasm->instance = wasm_runtime_instantiate_ex2(wasm->module, config, rt->error, sizeof(rt->error));
            wasm_runtime_instantiation_args_destroy(config);
            if (!wasm->instance) status = error(rt, NULL);
        }
    }
    if (!status) {
        wasm->exec_env = wasm_runtime_create_exec_env(wasm->instance, 16384);
        if (!wasm->exec_env) status = JFX_SCRIPT_OUT_OF_MEMORY;
    }
    uint32_t version = 0;
    bool abi_export = false;
    for (int32_t i = 0; !status && i < wasm_runtime_get_export_count(wasm->module); ++i) {
        wasm_export_t item; wasm_runtime_get_export_type(wasm->module, i, &item);
        if (!strcmp(item.name, "jfx_abi_version")) abi_export = true;
    }
    if (!status && abi_export) {
        uint8_t *data = NULL; size_t size = 0;
        if (!global_i32(wasm, "jfx_abi_version", &version) || version != JFX_WASM_ABI_VERSION || !global_i32(wasm, "jfx_scratch", &wasm->scratch) ||
            !global_i32(wasm, "jfx_scratch_size", &wasm->scratch_size)) status = JFX_SCRIPT_TYPE_ERROR;
        else if (!memory(wasm, &data, &size) || wasm->scratch_size < JFX_WASM_VALUE_SIZE ||
            (uint64_t)wasm->scratch + wasm->scratch_size > UINT32_MAX ||
            !span(size, wasm->scratch, wasm->scratch_size)) status = JFX_SCRIPT_TYPE_ERROR;
        else wasm->typed = true;
    }
    if (status) unload(wasm); else wasm->loaded = true;
    allocation_owner = previous;
    return status;
}
static jfx_script_status_t capture(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t *out) {
    wasm_runtime_t *wasm = as_wasm(rt);
    if (!wasm->loaded) return JFX_SCRIPT_NOT_FOUND;
    wasm_function_inst_t function = wasm_runtime_lookup_function(wasm->instance, name);
    if (!function) return JFX_SCRIPT_NOT_FOUND;
    jfx_wasm_ref_t value = { .callable = true, .function = function }; return pin(rt, &value, out);
}
static jfx_script_status_t call(jfx_script_runtime_t *rt, jfx_script_value_t ref,
    const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    wasm_runtime_t *wasm = as_wasm(rt);
    int slot = ref_slot(rt, ref);
    if (slot < 0 || !wasm->refs[slot].callable) return JFX_SCRIPT_TYPE_ERROR;
    wasm_function_inst_t function = wasm->refs[slot].function;
    uint32_t param_count = wasm_func_get_param_count(function, wasm->instance);
    uint32_t nresults = wasm_func_get_result_count(function, wasm->instance);
    if (param_count > JFX_SCRIPT_MAX_ARGS || nresults > 1) return JFX_SCRIPT_TYPE_ERROR;
    wasm_valkind_t params[JFX_SCRIPT_MAX_ARGS], results[1];
    wasm_func_get_param_types(function, wasm->instance, params);
    wasm_func_get_result_types(function, wasm->instance, results);
    wasm_val_t argv[JFX_SCRIPT_MAX_ARGS], result = {0};
    size_t nargs = argc; jfx_script_status_t status = JFX_SCRIPT_OK;
    uint32_t result_offset = 0;
    if (wasm->typed) {
        if (param_count != 3 || nresults != 1 || results[0] != WASM_I32) status = JFX_SCRIPT_TYPE_ERROR;
        for (size_t i = 0; i < param_count && !status; ++i) if (params[i] != WASM_I32) status = JFX_SCRIPT_TYPE_ERROR;
        uint8_t *data = NULL; size_t size = 0;
        if (!status && !memory(wasm, &data, &size)) status = JFX_SCRIPT_TYPE_ERROR;
        if (!status) {
            size_t records = (argc + 1) * JFX_WASM_VALUE_SIZE;
            if (!span(size, wasm->scratch, wasm->scratch_size) || records > wasm->scratch_size ||
                (uint64_t)wasm->scratch + wasm->scratch_size > UINT32_MAX) status = JFX_SCRIPT_TYPE_ERROR;
            else {
                result_offset = wasm->scratch + (uint32_t)argc * JFX_WASM_VALUE_SIZE;
                uint32_t cursor = result_offset + JFX_WASM_VALUE_SIZE, end = wasm->scratch + wasm->scratch_size;
                memset(data + wasm->scratch, 0, records);
                for (size_t i = 0; i < argc && !status; ++i)
                    status = write_value(rt, data, size, wasm->scratch + (uint32_t)i * JFX_WASM_VALUE_SIZE, &args[i], &cursor, end);
                argv[0] = (wasm_val_t){ .kind = WASM_I32, .of.i32 = (int32_t)wasm->scratch };
                argv[1] = (wasm_val_t){ .kind = WASM_I32, .of.i32 = (int32_t)argc };
                argv[2] = (wasm_val_t){ .kind = WASM_I32, .of.i32 = (int32_t)result_offset };
                nargs = 3;
            }
        }
    } else {
        if (param_count != argc) status = JFX_SCRIPT_TYPE_ERROR;
        for (size_t i = 0; i < argc && !status; ++i) {
            wasm_valkind_t kind = params[i];
            if (kind == WASM_I64 && args[i].type == JFX_TYPE_INT) argv[i] = (wasm_val_t){ .kind = WASM_I64, .of.i64 = args[i].i };
            else if (kind == WASM_I32 && args[i].type == JFX_TYPE_INT && args[i].i >= INT32_MIN && args[i].i <= INT32_MAX)
                argv[i] = (wasm_val_t){ .kind = WASM_I32, .of.i32 = (int32_t)args[i].i };
            else if (kind == WASM_F64 && args[i].type == JFX_TYPE_FLOAT) argv[i] = (wasm_val_t){ .kind = WASM_F64, .of.f64 = args[i].f };
            else status = JFX_SCRIPT_TYPE_ERROR;
        }
    }
    if (nresults && results[0] != WASM_I32 && results[0] != WASM_I64 && results[0] != WASM_F64) status = JFX_SCRIPT_TYPE_ERROR;
    if (status) return status;
    if (rt->instructions >= rt->instruction_limit) return JFX_SCRIPT_BUDGET;
    uint64_t remaining = rt->instruction_limit - rt->instructions;
    wasm_runtime_set_instruction_count_limit(wasm->exec_env, remaining > INT_MAX ? INT_MAX : (int)remaining);
    wasm_runtime_set_exception(wasm->instance, NULL);
    jfx_script_runtime_t *previous = allocation_owner; allocation_owner = rt;
    bool success = wasm_runtime_call_wasm_a(wasm->exec_env, function, nresults, &result, (uint32_t)nargs, argv);
    allocation_owner = previous;
    if (!success) return error(rt, wasm_runtime_get_exception(wasm->instance));
    if (wasm->typed) {
        if (result.of.i32) return result.of.i32 <= JFX_SCRIPT_INVALID_ARGUMENT && result.of.i32 >= JFX_SCRIPT_CAPABILITY ?
            (jfx_script_status_t)result.of.i32 : JFX_SCRIPT_ERROR;
        uint8_t *data = NULL; size_t size = 0;
        if (!memory(wasm, &data, &size)) return JFX_SCRIPT_TYPE_ERROR;
        return read_value(rt, data, size, result_offset, true, NULL, out);
    }
    *out = (jfx_value_t){0};
    if (!nresults) return JFX_SCRIPT_OK;
    switch (result.kind) {
        case WASM_I32: out->type = JFX_TYPE_INT; out->i = result.of.i32; break;
        case WASM_I64: out->type = JFX_TYPE_INT; out->i = result.of.i64; break;
        case WASM_F64: out->type = JFX_TYPE_FLOAT; out->f = result.of.f64; if (!isfinite(out->f)) return JFX_SCRIPT_TYPE_ERROR; break;
        default: return JFX_SCRIPT_TYPE_ERROR;
    }
    return JFX_SCRIPT_OK;
}
static jfx_script_status_t from(jfx_script_runtime_t *rt, const jfx_value_t *v, jfx_script_value_t *out) {
    if (v->type == JFX_TYPE_FUNCTION) {
        int slot = ref_slot(rt, v->function); if (slot < 0) return JFX_SCRIPT_TYPE_ERROR;
        return pin(rt, &as_wasm(rt)->refs[slot], out);
    }
    jfx_wasm_ref_t value = { .value = *v }; return pin(rt, &value, out);
}
static jfx_script_status_t to(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_value_t *out) {
    int slot = ref_slot(rt, ref); if (slot < 0) return JFX_SCRIPT_TYPE_ERROR;
    jfx_wasm_ref_t *entry = &as_wasm(rt)->refs[slot];
    if (entry->callable) { out->type = JFX_TYPE_FUNCTION; return pin(rt, entry, &out->function); }
    if (entry->value.type == JFX_TYPE_STRING) return jfx_script_copy_string(rt, entry->value.str, strlen(entry->value.str), out);
    *out = entry->value; return JFX_SCRIPT_OK;
}
static jfx_script_status_t clone(jfx_script_runtime_t *rt, jfx_script_value_t ref, jfx_script_value_t *out) {
    int slot = ref_slot(rt, ref); return slot < 0 ? JFX_SCRIPT_TYPE_ERROR : pin(rt, &as_wasm(rt)->refs[slot], out);
}
static void gc(jfx_script_runtime_t *rt, int mode) {
    (void)rt; (void)mode;
}
static void destroy(jfx_script_runtime_t *rt) {
    wasm_runtime_t *wasm = as_wasm(rt);
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) (void)jfx_script_alloc(rt, wasm->refs[i].string, 0);
    jfx_script_runtime_t *previous = allocation_owner; allocation_owner = rt;
    unload(wasm);
    allocation_owner = NULL;
    if (!--runtime_count) wasm_runtime_destroy();
    allocation_owner = previous;
    jfx_script_base_free(rt);
}
jfx_script_status_t jfx_wasm_script_create(const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    static const jfx_script_ops_t ops = { destroy, load, capture, call, from, to, clone, release, gc };
    jfx_script_status_t status = jfx_script_base_create(sizeof(wasm_runtime_t), desc, JFX_SCRIPT_WASM, &ops, out);
    if (status) return status;
    if (!runtime_count) {
        RuntimeInitArgs config = { .mem_alloc_type = Alloc_With_Allocator, .running_mode = Mode_Interp };
        /* Upstream represents callback pointers as void*. Keep the ABI
         * conversion localized without function/object pointer casts. */
        union { void *object; void *(*function)(mem_alloc_usage_t, unsigned int); } alloc_cb = { .function = wamr_malloc };
        union { void *object; void *(*function)(mem_alloc_usage_t, bool, void *, unsigned int); } realloc_cb = { .function = wamr_realloc };
        union { void *object; void (*function)(mem_alloc_usage_t, void *); } free_cb = { .function = wamr_free };
        union { void *object; void (*function)(wasm_exec_env_t, uint64_t *); } call_cb = { .function = host_call },
            clamp_cb = { .function = host_clamp };
        config.mem_alloc_option.allocator.malloc_func = alloc_cb.object;
        config.mem_alloc_option.allocator.realloc_func = realloc_cb.object;
        config.mem_alloc_option.allocator.free_func = free_cb.object;
        static NativeSymbol symbols[] = {
            { "call", NULL, "(iiiii)i", NULL },
            { "clamp", NULL, "(FFF)F", NULL }
        };
        symbols[0].func_ptr = call_cb.object; symbols[1].func_ptr = clamp_cb.object;
        jfx_script_runtime_t *previous = allocation_owner; allocation_owner = NULL;
        bool initialized = wasm_runtime_full_init(&config);
        bool linked = initialized && wasm_runtime_register_natives_raw("joltfx", symbols, 2);
        allocation_owner = previous;
        if (!linked) {
            if (initialized) wasm_runtime_destroy();
            jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY;
        }
        wasm_runtime_set_log_level(WASM_LOG_LEVEL_FATAL);
    }
    ++runtime_count;
    return JFX_SCRIPT_OK;
}
