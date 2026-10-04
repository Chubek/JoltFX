#ifndef JFX_SCRIPT_RUNTIME_H
#define JFX_SCRIPT_RUNTIME_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "jfx/jfx_editor.h"
#include "jfx/jfx_buffer.h"
#include "jfx/jfx_texture.h"
#include "tilly/allocator.h"
#include "tillyz/tillyz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JFX_SCRIPT_OK = 0,
    JFX_SCRIPT_INVALID_ARGUMENT = -1,
    JFX_SCRIPT_OUT_OF_MEMORY = -2,
    JFX_SCRIPT_ERROR = -3,
    JFX_SCRIPT_NOT_FOUND = -4,
    JFX_SCRIPT_BUDGET = -5,
    JFX_SCRIPT_TYPE_ERROR = -6,
    JFX_SCRIPT_BUSY = -7,
    JFX_SCRIPT_NOT_SUPPORTED = -8,
    JFX_SCRIPT_CAPABILITY = -9,
} jfx_script_status_t;

typedef struct {
    size_t memory_limit;
    uint64_t instruction_limit;
} jfx_script_config_t;

#define JFX_SCRIPT_API_MAJOR 1
#define JFX_SCRIPT_API_MINOR 0
#define JFX_SCRIPT_MAX_ARGS 16

typedef enum {
    JFX_SCRIPT_LUA, JFX_SCRIPT_MRUBY, JFX_SCRIPT_QUICKJS,
    JFX_SCRIPT_PYTHON, JFX_SCRIPT_WASM, JFX_SCRIPT_LANGUAGE_COUNT
} jfx_script_language_t;

typedef enum {
    JFX_TYPE_NIL, JFX_TYPE_BOOL, JFX_TYPE_INT, JFX_TYPE_FLOAT,
    JFX_TYPE_VEC2, JFX_TYPE_VEC3, JFX_TYPE_VEC4, JFX_TYPE_COLOR,
    JFX_TYPE_BUFFER, JFX_TYPE_TEXTURE, JFX_TYPE_STRING, JFX_TYPE_USERDATA,
    JFX_TYPE_FUNCTION
} jfx_value_type_t;

/* References are runtime-local capabilities, never native addresses. */
typedef uint64_t jfx_script_value_t;
typedef struct {
    jfx_value_type_t type;
    union {
        bool b;
        int64_t i;
        double f;
        float vec2[2], vec3[3], vec4[4];
        uint32_t color;
        jfx_buffer_t *buffer;
        jfx_texture_t *texture;
        const char *str;
        void *userdata;
        jfx_script_value_t function;
    };
} jfx_value_t;

typedef enum {
    JFX_SCRIPT_CAP_EDITOR = 1u << 0,
    JFX_SCRIPT_CAP_EVENTS = 1u << 1
} jfx_script_capability_t;

typedef struct {
    size_t size;
    jfx_script_config_t config;
    /* Borrowed; must outlive the runtime. NULL selects Tilly's allocator. */
    tilly_allocator_t *allocator;
    jfx_editor_t *editor;
    uint32_t capabilities;
    /* Optional bootstrap error sink. */
    tillyz_context_t *error_context;
} jfx_script_desc_t;

typedef struct jfx_script_runtime jfx_script_runtime_t;
bool jfx_script_language_available(jfx_script_language_t language);
const char *jfx_script_language_name(jfx_script_language_t language);
jfx_script_status_t jfx_script_runtime_create(jfx_script_language_t language,
    const jfx_script_desc_t *desc, jfx_script_runtime_t **out_runtime);
void jfx_script_runtime_destroy(jfx_script_runtime_t *runtime);
/* Text-only for scripting languages; Wasmtime accepts validated .wasm or WAT.
 * Loading is additive. Failed scripts can leave globals/edits made before an
 * exception; runtime-owned registrations made by a failed load are rolled back. */
jfx_script_status_t jfx_script_runtime_load(jfx_script_runtime_t *runtime,
    const void *source, size_t length, const char *name);
/* Output is unchanged on error. Strings in successful results are borrowed until
 * the next execution/translation on this runtime. Resources are invocation scoped. */
jfx_script_status_t jfx_script_runtime_call(jfx_script_runtime_t *runtime,
    const char *name, const jfx_value_t *args, size_t argc, jfx_value_t *out_result);
jfx_script_status_t jfx_script_runtime_call_number(jfx_script_runtime_t *runtime,
    const char *name, double input, double *out_result);
/* Local batch functions, not engine kernel registration. Captured callables
 * remain stable if a script subsequently replaces a global with the same name. */
jfx_script_status_t jfx_script_runtime_register_kernel(jfx_script_runtime_t *runtime,
    const char *name, const char *function_name);
jfx_script_status_t jfx_script_runtime_invoke_kernel(jfx_script_runtime_t *runtime,
    const char *name, const jfx_value_t *args, size_t argc, jfx_value_t *out_result);
jfx_script_status_t jfx_script_runtime_from_native(jfx_script_runtime_t *runtime,
    const jfx_value_t *value, jfx_script_value_t *out_value);
jfx_script_status_t jfx_script_runtime_to_native(jfx_script_runtime_t *runtime,
    jfx_script_value_t value, jfx_value_t *out_value);
void jfx_script_runtime_release_value(jfx_script_runtime_t *runtime, jfx_script_value_t value);
jfx_script_status_t jfx_script_runtime_on(jfx_script_runtime_t *runtime,
    const char *event_name, const char *function_name, uint32_t *out_subscription);
jfx_script_status_t jfx_script_runtime_off(jfx_script_runtime_t *runtime, uint32_t subscription);
const char *jfx_script_runtime_last_error(const jfx_script_runtime_t *runtime);
jfx_script_status_t jfx_script_runtime_last_status(const jfx_script_runtime_t *runtime);
size_t jfx_script_runtime_memory_used(const jfx_script_runtime_t *runtime);
size_t jfx_script_runtime_memory_limit(const jfx_script_runtime_t *runtime);
void jfx_script_runtime_gc_collect(jfx_script_runtime_t *runtime);
void jfx_script_runtime_gc_pause(jfx_script_runtime_t *runtime);
void jfx_script_runtime_gc_resume(jfx_script_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif
