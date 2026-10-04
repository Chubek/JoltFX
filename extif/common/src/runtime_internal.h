#ifndef JFX_SCRIPT_INTERNAL_H
#define JFX_SCRIPT_INTERNAL_H
#include "jfx/ffi_bridge.h"
#include "jfx/sandbox.h"
#include "jfx/jfx_events.h"

#define JFX_SCRIPT_REFS 128
#define JFX_SCRIPT_REGISTRATIONS 32
#define JFX_SCRIPT_RESOURCES 32

typedef struct {
    void (*destroy)(jfx_script_runtime_t *);
    jfx_script_status_t (*load)(jfx_script_runtime_t *, const void *, size_t, const char *);
    jfx_script_status_t (*capture)(jfx_script_runtime_t *, const char *, jfx_script_value_t *);
    jfx_script_status_t (*call)(jfx_script_runtime_t *, jfx_script_value_t, const jfx_value_t *, size_t, jfx_value_t *);
    jfx_script_status_t (*from)(jfx_script_runtime_t *, const jfx_value_t *, jfx_script_value_t *);
    jfx_script_status_t (*to)(jfx_script_runtime_t *, jfx_script_value_t, jfx_value_t *);
    jfx_script_status_t (*clone)(jfx_script_runtime_t *, jfx_script_value_t, jfx_script_value_t *);
    void (*release)(jfx_script_runtime_t *, jfx_script_value_t);
    void (*gc)(jfx_script_runtime_t *, int);
} jfx_script_ops_t;

typedef struct {
    jfx_script_runtime_t *runtime;
    jfx_script_value_t function;
    jfx_event_type_t event;
    uint32_t id;
    bool remove_pending;
} jfx_script_callback_t;
typedef struct { char name[96]; jfx_script_value_t function; } jfx_script_kernel_t;
struct jfx_script_runtime {
    const jfx_script_ops_t *ops;
    tilly_allocator_t *allocator;
    jfx_editor_t *editor;
    tillyz_context_t *error_context;
    size_t memory_limit, memory_used;
    uint64_t instruction_limit, instructions, epoch;
    uint32_t capabilities, next_subscription, reference_namespace;
    jfx_script_language_t language;
    jfx_script_status_t fault, last_status;
    bool busy, gc_paused;
    char error[2048];
    char *result_string;
    jfx_value_t resources[JFX_SCRIPT_RESOURCES];
    size_t resource_count;
    jfx_script_callback_t callbacks[JFX_SCRIPT_REGISTRATIONS];
    jfx_script_kernel_t kernels[JFX_SCRIPT_REGISTRATIONS];
};

jfx_script_status_t jfx_script_base_create(size_t bytes, const jfx_script_desc_t *desc,
    jfx_script_language_t language, const jfx_script_ops_t *ops, jfx_script_runtime_t **out);
void jfx_script_base_free(jfx_script_runtime_t *rt);
void *jfx_script_alloc(jfx_script_runtime_t *rt, void *ptr, size_t size);
size_t jfx_script_alloc_size(const void *ptr);
jfx_script_status_t jfx_script_fail(jfx_script_runtime_t *rt, jfx_script_status_t status, const char *message);
bool jfx_script_tick(jfx_script_runtime_t *rt, uint64_t count);
jfx_script_status_t jfx_script_copy_string(jfx_script_runtime_t *rt, const char *text, size_t length, jfx_value_t *out);
jfx_script_status_t jfx_script_wrap(jfx_script_runtime_t *rt, const jfx_value_t *value, uint64_t *out_token);
jfx_script_status_t jfx_script_unwrap(jfx_script_runtime_t *rt, uint64_t token, jfx_value_t *out);
jfx_script_status_t jfx_script_host_call(jfx_script_runtime_t *rt, const char *name,
    const jfx_value_t *args, size_t argc, jfx_value_t *out);
extern const char *const jfx_script_host_names[];
extern const size_t jfx_script_host_count;
static inline jfx_script_value_t jfx_script_ref_token(jfx_script_runtime_t *rt, uint32_t generation, size_t slot) {
    return (uint64_t)rt->reference_namespace << 32 | (uint64_t)(generation & 0xffffffu) << 8 | (slot + 1);
}
static inline bool jfx_script_ref_matches(jfx_script_runtime_t *rt, jfx_script_value_t token, uint32_t generation) {
    return (uint32_t)(token >> 32) == rt->reference_namespace &&
        (uint32_t)((token >> 8) & 0xffffffu) == (generation & 0xffffffu);
}
/* Adapter factories are called only by the language-neutral factory. */
jfx_script_status_t jfx_lua_script_create(const jfx_script_desc_t *, jfx_script_runtime_t **);
jfx_script_status_t jfx_mruby_script_create(const jfx_script_desc_t *, jfx_script_runtime_t **);
jfx_script_status_t jfx_quickjs_script_create(const jfx_script_desc_t *, jfx_script_runtime_t **);
jfx_script_status_t jfx_python_script_create(const jfx_script_desc_t *, jfx_script_runtime_t **);
jfx_script_status_t jfx_wasm_script_create(const jfx_script_desc_t *, jfx_script_runtime_t **);
#endif
