#include "jfx/wasm.h"
#include "runtime_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "wasmtime.h"

typedef struct {
    bool used, callable;
    uint32_t generation;
    wasmtime_func_t function;
    jfx_value_t value;
    char *string;
} jfx_wasm_ref_t;
typedef struct {
    jfx_script_runtime_t base;
    wasm_engine_t *engine;
    wasmtime_store_t *store;
    wasmtime_context_t *context;
    wasmtime_module_t *module;
    wasmtime_instance_t instance;
    bool loaded, typed;
    uint32_t scratch, scratch_size;
    jfx_wasm_ref_t refs[JFX_SCRIPT_REFS];
} wasm_runtime_t;
typedef struct { jfx_script_runtime_t *rt; uint8_t *data; size_t size, maximum; } jfx_wasm_memory_t;
static wasm_runtime_t *as_wasm(jfx_script_runtime_t *rt) { return (wasm_runtime_t *)rt; }

static jfx_script_status_t error(jfx_script_runtime_t *rt, wasmtime_error_t *err, wasm_trap_t *trap) {
    wasm_byte_vec_t text = {0};
    if (err) wasmtime_error_message(err, &text); else if (trap) wasm_trap_message(trap, &text);
    jfx_script_status_t status = rt->fault ? rt->fault : JFX_SCRIPT_ERROR;
    if (trap) { wasmtime_trap_code_t code; if (wasmtime_trap_code(trap, &code) && code == WASMTIME_TRAP_CODE_OUT_OF_FUEL) status = JFX_SCRIPT_BUDGET; }
    if (text.data) (void)snprintf(rt->error, sizeof(rt->error), "%.*s", (int)fmin((double)text.size, 2047), text.data);
    wasm_byte_vec_delete(&text);
    if (err) wasmtime_error_delete(err);
    if (trap) wasm_trap_delete(trap);
    return status;
}
static uint8_t *memory_get(void *user, size_t *size, size_t *capacity) {
    jfx_wasm_memory_t *mem = user; *size = mem->size; *capacity = mem->size; return mem->data;
}
static wasmtime_error_t *memory_grow(void *user, size_t size) {
    jfx_wasm_memory_t *mem = user;
    if (size > mem->maximum) return wasmtime_error_new("linear memory limit exceeded");
    uint8_t *next = jfx_script_alloc(mem->rt, mem->data, size ? size : 1);
    if (!next) return wasmtime_error_new("linear memory budget exceeded");
    if (size > mem->size) memset(next + mem->size, 0, size - mem->size);
    mem->data = next; mem->size = size; return NULL;
}
static void memory_free(void *user) {
    jfx_wasm_memory_t *mem = user; jfx_script_runtime_t *rt = mem->rt;
    (void)jfx_script_alloc(rt, mem->data, 0); (void)jfx_script_alloc(rt, mem, 0);
}
static wasmtime_error_t *memory_new(void *user, const wasm_memorytype_t *type,
    size_t minimum, size_t maximum, size_t reserved, size_t guard, wasmtime_linear_memory_t *out) {
    (void)type; (void)reserved;
    if (guard) return wasmtime_error_new("guarded memory is not enabled");
    jfx_script_runtime_t *rt = user;
    jfx_wasm_memory_t *mem = jfx_script_alloc(rt, NULL, sizeof(*mem));
    if (!mem) return wasmtime_error_new("linear memory budget exceeded");
    *mem = (jfx_wasm_memory_t){ .rt = rt, .maximum = maximum < rt->memory_limit ? maximum : rt->memory_limit };
    wasmtime_error_t *err = memory_grow(mem, minimum);
    if (err) { memory_free(mem); return err; }
    *out = (wasmtime_linear_memory_t){ mem, memory_get, memory_grow, memory_free }; return NULL;
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
static wasm_trap_t *host_call(void *user, wasmtime_caller_t *caller,
    const wasmtime_val_t *argv, size_t argc, wasmtime_val_t *ret, size_t nret) {
    (void)argc; (void)nret;
    jfx_script_runtime_t *rt = user;
    wasmtime_extern_t ext;
    jfx_script_status_t status = JFX_SCRIPT_TYPE_ERROR;
    if (wasmtime_caller_export_get(caller, "memory", 6, &ext) && ext.kind == WASMTIME_EXTERN_MEMORY) {
        wasmtime_context_t *context = wasmtime_caller_context(caller);
        uint8_t *data = wasmtime_memory_data(context, &ext.of.memory);
        size_t size = wasmtime_memory_data_size(context, &ext.of.memory);
        uint32_t op = (uint32_t)argv[0].of.i32, length = (uint32_t)argv[1].of.i32;
        uint32_t args_offset = (uint32_t)argv[2].of.i32, count = (uint32_t)argv[3].of.i32;
        uint32_t result_offset = (uint32_t)argv[4].of.i32;
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
            if (!status) status = write_value(rt, data, size, result_offset, &result, NULL, 0);
            for (uint32_t i = 0; i < count; ++i) (void)jfx_script_alloc(rt, strings[i], 0);
        }
    }
    ret[0].kind = WASMTIME_I32; ret[0].of.i32 = status;
    if (status) (void)jfx_script_fail(rt, status, "joltwasm host call failed");
    return NULL;
}
static wasm_trap_t *host_clamp(void *user, wasmtime_caller_t *caller,
    const wasmtime_val_t *args, size_t argc, wasmtime_val_t *ret, size_t nret) {
    (void)caller; (void)argc; (void)nret;
    jfx_script_runtime_t *rt = user;
    jfx_value_t values[3], result = {0};
    for (size_t i = 0; i < 3; ++i) values[i] = (jfx_value_t){ .type = JFX_TYPE_FLOAT, .f = args[i].of.f64 };
    jfx_script_status_t status = jfx_script_host_call(rt, "clamp", values, 3, &result);
    if (status) { (void)jfx_script_fail(rt, status, "joltwasm clamp failed"); return wasmtime_trap_new(rt->error, strlen(rt->error)); }
    ret[0].kind = WASMTIME_F64; ret[0].of.f64 = result.f; return NULL;
}
static bool global_i32(wasm_runtime_t *wasm, const char *name, uint32_t *out) {
    wasmtime_extern_t ext;
    if (!wasmtime_instance_export_get(wasm->context, &wasm->instance, name, strlen(name), &ext) || ext.kind != WASMTIME_EXTERN_GLOBAL) return false;
    wasm_globaltype_t *type = wasmtime_global_type(wasm->context, &ext.of.global);
    bool immutable = wasm_globaltype_mutability(type) == WASM_CONST;
    wasm_globaltype_delete(type);
    wasmtime_val_t value; wasmtime_global_get(wasm->context, &ext.of.global, &value);
    if (value.kind != WASMTIME_I32) { wasmtime_val_unroot(&value); return false; }
    if (!immutable || value.of.i32 < 0) return false;
    *out = (uint32_t)value.of.i32; return true;
}
static jfx_script_status_t load(jfx_script_runtime_t *rt, const void *source, size_t length, const char *name) {
    (void)name;
    wasm_runtime_t *wasm = as_wasm(rt);
    if (wasm->loaded) return JFX_SCRIPT_BUSY;
    wasm_byte_vec_t binary = {0};
    const uint8_t *bytes = source;
    wasmtime_error_t *err = NULL;
    if (length < 4 || memcmp(source, "\0asm", 4)) {
        err = wasmtime_wat2wasm(source, length, &binary);
        if (err) return error(rt, err, NULL);
        bytes = (const uint8_t *)binary.data; length = binary.size;
    }
    err = wasmtime_module_new(wasm->engine, bytes, length, &wasm->module);
    wasm_byte_vec_delete(&binary);
    if (err) return error(rt, err, NULL);
    wasm->store = wasmtime_store_new(wasm->engine, rt, NULL);
    if (!wasm->store) { wasmtime_module_delete(wasm->module); wasm->module = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    wasm->context = wasmtime_store_context(wasm->store);
    wasmtime_store_limiter(wasm->store, (int64_t)fmin((double)rt->memory_limit, (double)INT64_MAX), 128, 1, 1, 1);
    err = wasmtime_context_set_fuel(wasm->context, rt->instruction_limit);
    wasmtime_linker_t *linker = wasmtime_linker_new(wasm->engine);
    wasm_valtype_vec_t params, results;
    wasm_valtype_vec_new_uninitialized(&params, 5);
    for (size_t i = 0; i < 5; ++i) params.data[i] = wasm_valtype_new_i32();
    wasm_valtype_vec_new_uninitialized(&results, 1); results.data[0] = wasm_valtype_new_i32();
    wasm_functype_t *type = wasm_functype_new(&params, &results);
    if (!err) err = wasmtime_linker_define_func(linker, "joltfx", 6, "call", 4, type, host_call, rt, NULL);
    wasm_functype_delete(type);
    wasm_valtype_vec_new_uninitialized(&params, 3);
    for (size_t i = 0; i < 3; ++i) params.data[i] = wasm_valtype_new_f64();
    wasm_valtype_vec_new_uninitialized(&results, 1); results.data[0] = wasm_valtype_new_f64();
    type = wasm_functype_new(&params, &results);
    if (!err) err = wasmtime_linker_define_func(linker, "joltfx", 6, "clamp", 5, type, host_clamp, rt, NULL);
    wasm_functype_delete(type);
    wasm_trap_t *trap = NULL;
    if (!err) err = wasmtime_linker_instantiate(linker, wasm->context, wasm->module, &wasm->instance, &trap);
    wasmtime_linker_delete(linker);
    jfx_script_status_t status = err || trap ? error(rt, err, trap) : JFX_SCRIPT_OK;
    uint32_t version = 0;
    wasmtime_extern_t ext;
    if (!status && wasmtime_instance_export_get(wasm->context, &wasm->instance, "jfx_abi_version", 15, &ext)) {
        if (!global_i32(wasm, "jfx_abi_version", &version) || version != JFX_WASM_ABI_VERSION || !global_i32(wasm, "jfx_scratch", &wasm->scratch) ||
            !global_i32(wasm, "jfx_scratch_size", &wasm->scratch_size)) status = JFX_SCRIPT_TYPE_ERROR;
        else if (!wasmtime_instance_export_get(wasm->context, &wasm->instance, "memory", 6, &ext) ||
            ext.kind != WASMTIME_EXTERN_MEMORY || wasm->scratch_size < JFX_WASM_VALUE_SIZE ||
            (uint64_t)wasm->scratch + wasm->scratch_size > UINT32_MAX ||
            !span(wasmtime_memory_data_size(wasm->context, &ext.of.memory), wasm->scratch, wasm->scratch_size)) status = JFX_SCRIPT_TYPE_ERROR;
        else wasm->typed = true;
    }
    if (status) {
        wasmtime_store_delete(wasm->store); wasm->store = NULL; wasm->context = NULL;
        wasmtime_module_delete(wasm->module); wasm->module = NULL;
    } else wasm->loaded = true;
    return status;
}
static jfx_script_status_t capture(jfx_script_runtime_t *rt, const char *name, jfx_script_value_t *out) {
    wasm_runtime_t *wasm = as_wasm(rt); wasmtime_extern_t ext;
    if (!wasm->loaded || !wasmtime_instance_export_get(wasm->context, &wasm->instance, name, strlen(name), &ext) ||
        ext.kind != WASMTIME_EXTERN_FUNC) return JFX_SCRIPT_NOT_FOUND;
    jfx_wasm_ref_t value = { .callable = true, .function = ext.of.func }; return pin(rt, &value, out);
}
static jfx_script_status_t call(jfx_script_runtime_t *rt, jfx_script_value_t ref,
    const jfx_value_t *args, size_t argc, jfx_value_t *out) {
    wasm_runtime_t *wasm = as_wasm(rt);
    int slot = ref_slot(rt, ref);
    if (slot < 0 || !wasm->refs[slot].callable) return JFX_SCRIPT_TYPE_ERROR;
    wasmtime_func_t *function = &wasm->refs[slot].function;
    wasm_functype_t *type = wasmtime_func_type(wasm->context, function);
    const wasm_valtype_vec_t *params = wasm_functype_params(type), *results = wasm_functype_results(type);
    wasmtime_val_t argv[JFX_SCRIPT_MAX_ARGS], result = {0};
    size_t nargs = argc; jfx_script_status_t status = JFX_SCRIPT_OK;
    uint32_t result_offset = 0;
    if (wasm->typed) {
        if (params->size != 3 || results->size != 1 || wasm_valtype_kind(results->data[0]) != WASM_I32) status = JFX_SCRIPT_TYPE_ERROR;
        for (size_t i = 0; i < params->size && !status; ++i) if (wasm_valtype_kind(params->data[i]) != WASM_I32) status = JFX_SCRIPT_TYPE_ERROR;
        wasmtime_extern_t ext;
        if (!status && (!wasmtime_instance_export_get(wasm->context, &wasm->instance, "memory", 6, &ext) ||
            ext.kind != WASMTIME_EXTERN_MEMORY)) status = JFX_SCRIPT_TYPE_ERROR;
        if (!status) {
            uint8_t *data = wasmtime_memory_data(wasm->context, &ext.of.memory);
            size_t size = wasmtime_memory_data_size(wasm->context, &ext.of.memory);
            size_t records = (argc + 1) * JFX_WASM_VALUE_SIZE;
            if (!span(size, wasm->scratch, wasm->scratch_size) || records > wasm->scratch_size ||
                (uint64_t)wasm->scratch + wasm->scratch_size > UINT32_MAX) status = JFX_SCRIPT_TYPE_ERROR;
            else {
                result_offset = wasm->scratch + (uint32_t)argc * JFX_WASM_VALUE_SIZE;
                uint32_t cursor = result_offset + JFX_WASM_VALUE_SIZE, end = wasm->scratch + wasm->scratch_size;
                memset(data + wasm->scratch, 0, records);
                for (size_t i = 0; i < argc && !status; ++i)
                    status = write_value(rt, data, size, wasm->scratch + (uint32_t)i * JFX_WASM_VALUE_SIZE, &args[i], &cursor, end);
                argv[0] = (wasmtime_val_t){ .kind = WASMTIME_I32, .of.i32 = (int32_t)wasm->scratch };
                argv[1] = (wasmtime_val_t){ .kind = WASMTIME_I32, .of.i32 = (int32_t)argc };
                argv[2] = (wasmtime_val_t){ .kind = WASMTIME_I32, .of.i32 = (int32_t)result_offset };
                nargs = 3;
            }
        }
    } else {
        if (params->size != argc || results->size > 1) status = JFX_SCRIPT_TYPE_ERROR;
        for (size_t i = 0; i < argc && !status; ++i) {
            wasm_valkind_t kind = wasm_valtype_kind(params->data[i]);
            if (kind == WASM_I64 && args[i].type == JFX_TYPE_INT) argv[i] = (wasmtime_val_t){ .kind = WASMTIME_I64, .of.i64 = args[i].i };
            else if (kind == WASM_I32 && args[i].type == JFX_TYPE_INT && args[i].i >= INT32_MIN && args[i].i <= INT32_MAX)
                argv[i] = (wasmtime_val_t){ .kind = WASMTIME_I32, .of.i32 = (int32_t)args[i].i };
            else if (kind == WASM_F64 && args[i].type == JFX_TYPE_FLOAT) argv[i] = (wasmtime_val_t){ .kind = WASMTIME_F64, .of.f64 = args[i].f };
            else status = JFX_SCRIPT_TYPE_ERROR;
        }
    }
    size_t nresults = results->size; wasm_functype_delete(type);
    if (status) return status;
    wasmtime_error_t *err = wasmtime_context_set_fuel(wasm->context, rt->instruction_limit - rt->instructions);
    wasm_trap_t *trap = NULL;
    if (!err) err = wasmtime_func_call(wasm->context, function, argv, nargs, &result, nresults, &trap);
    if (err || trap) return error(rt, err, trap);
    if (wasm->typed) {
        if (result.of.i32) return result.of.i32 <= JFX_SCRIPT_INVALID_ARGUMENT && result.of.i32 >= JFX_SCRIPT_CAPABILITY ?
            (jfx_script_status_t)result.of.i32 : JFX_SCRIPT_ERROR;
        wasmtime_extern_t ext;
        if (!wasmtime_instance_export_get(wasm->context, &wasm->instance, "memory", 6, &ext)) return JFX_SCRIPT_TYPE_ERROR;
        return read_value(rt, wasmtime_memory_data(wasm->context, &ext.of.memory),
            wasmtime_memory_data_size(wasm->context, &ext.of.memory), result_offset, true, NULL, out);
    }
    *out = (jfx_value_t){0};
    if (!nresults) return JFX_SCRIPT_OK;
    switch (result.kind) {
        case WASMTIME_I32: out->type = JFX_TYPE_INT; out->i = result.of.i32; break;
        case WASMTIME_I64: out->type = JFX_TYPE_INT; out->i = result.of.i64; break;
        case WASMTIME_F64: out->type = JFX_TYPE_FLOAT; out->f = result.of.f64; if (!isfinite(out->f)) return JFX_SCRIPT_TYPE_ERROR; break;
        default: wasmtime_val_unroot(&result); return JFX_SCRIPT_TYPE_ERROR;
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
    if (!mode && as_wasm(rt)->context) wasmtime_context_gc(as_wasm(rt)->context);
}
static void destroy(jfx_script_runtime_t *rt) {
    wasm_runtime_t *wasm = as_wasm(rt);
    for (size_t i = 0; i < JFX_SCRIPT_REFS; ++i) (void)jfx_script_alloc(rt, wasm->refs[i].string, 0);
    if (wasm->store) wasmtime_store_delete(wasm->store);
    if (wasm->module) wasmtime_module_delete(wasm->module);
    if (wasm->engine) wasm_engine_delete(wasm->engine);
    jfx_script_base_free(rt);
}
jfx_script_status_t jfx_wasm_script_create(const jfx_script_desc_t *desc, jfx_script_runtime_t **out) {
    static const jfx_script_ops_t ops = { destroy, load, capture, call, from, to, clone, release, gc };
    jfx_script_status_t status = jfx_script_base_create(sizeof(wasm_runtime_t), desc, JFX_SCRIPT_WASM, &ops, out);
    if (status) return status;
    wasm_config_t *config = wasm_config_new();
    if (!config) { jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    wasmtime_config_consume_fuel_set(config, true);
    wasmtime_config_parallel_compilation_set(config, false);
    wasmtime_config_memory_reservation_set(config, 0);
    wasmtime_config_memory_guard_size_set(config, 0);
    wasmtime_config_memory_reservation_for_growth_set(config, 0);
    wasmtime_config_memory_may_move_set(config, true);
    wasmtime_config_memory_init_cow_set(config, false);
    wasmtime_memory_creator_t creator = { .env = *out, .new_memory = memory_new };
    wasmtime_config_host_memory_creator_set(config, &creator);
    as_wasm(*out)->engine = wasm_engine_new_with_config(config);
    if (!as_wasm(*out)->engine) { jfx_script_base_free(*out); *out = NULL; return JFX_SCRIPT_OUT_OF_MEMORY; }
    return JFX_SCRIPT_OK;
}
