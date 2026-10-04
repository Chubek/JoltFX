#ifndef JFX_PLUGIN_SDK_H
#define JFX_PLUGIN_SDK_H

#include "jfx_plugin.h"
#include "jfx_editor.h"
#include "jfx_events.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Host-service ABI: modules need only these headers, never engine linkage.
 * Native modules export jfx_plugin_entry. Statically linked modules use the
 * same definition with jfx_plugin_host_attach (including WASM/mobile builds). */
#define JFX_PLUGIN_SDK_MAJOR 1u
#define JFX_PLUGIN_SDK_MINOR 0u
#define JFX_PLUGIN_SDK_PATCH 0u
/* Packed version components: 8-bit major, 12-bit minor and 12-bit patch. */
#define JFX_PLUGIN_VERSION(major,minor,patch) (((uint32_t)(major)<<24u)|((uint32_t)(minor)<<12u)|(uint32_t)(patch))
#define JFX_PLUGIN_MAX_EFFECTS 128u
#if defined(_WIN32)
#define JFX_PLUGIN_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define JFX_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define JFX_PLUGIN_EXPORT
#endif

typedef struct jfx_plugin_api jfx_plugin_api_t;

/* Borrowed, tightly packed straight float RGBA. Write every output sample;
 * honor scratch_limit and use api->allocate/deallocate for scratch. Finite
 * output is published only on success. Alpha must remain in [0,1]. CPU
 * callbacks execute synchronously on the caller; no engine worker is created. */
typedef struct {
    size_t size;
    uint32_t width, height;
    double seconds;
    size_t scratch_limit;
    const float *input;
    const float *parameters;
    size_t parameter_count;
    float *output;
} jfx_plugin_image_t;
typedef jfx_result_t (*jfx_plugin_process_fn)(void *userdata,const jfx_plugin_image_t *image);
typedef struct {
    size_t size;
    /* Effect/parameter identifiers: at most JFX_PLUGIN_MAX_ID_LENGTH bytes,
     * alphanumeric plus '.', '_' and '-'; labels/categories: at most
     * JFX_PLUGIN_MAX_NAME_LENGTH bytes. Descriptors and strings are copied. */
    const char *name; /* Stable namespaced identifier, e.g. org.example.tint. */
    const char *label;
    const char *category; /* Color Grading / Color Calibration select those sections. */
    size_t parameter_count;
    const jfx_param_desc_t *parameters;
    jfx_plugin_process_fn process;
    void *userdata;
} jfx_plugin_effect_desc_t;
/* Source is compiled once through Glue. Parameter descriptors are reflected
 * from (param name default min max integer). Library/source are copied by the
 * compiler; compiled programs use premultiplied RGBA, marshaled by the host. */
typedef struct {
    size_t size;
    const char *name, *label, *category;
    const char *library, *source;
} jfx_plugin_kernel_desc_t;

/* Actions receive the frontend's editor and zero-based selection. Call the
 * editor_command service to edit it. The host groups edits into one undo step
 * and rolls back failed actions. The declared document becomes the active
 * preview; undo/cancel restores the prior mode. State-query services supply
 * borrowed-document state as caller-owned JSON. */
typedef struct {
    size_t size;
    jfx_editor_t *editor;
    uint32_t track, clip, node;
} jfx_plugin_action_context_t;
typedef jfx_result_t (*jfx_plugin_action_fn)(void *userdata,const jfx_plugin_action_context_t *context);
typedef struct {
    size_t size;
    const char *name, *label;
    jfx_project_kind_t document;
    jfx_plugin_action_fn invoke;
    void *userdata;
} jfx_plugin_action_desc_t;
typedef struct {
    size_t size;
    char name[JFX_PLUGIN_MAX_NAME_LENGTH+1u];
    char label[JFX_PLUGIN_MAX_NAME_LENGTH+1u];
    uint32_t plugin_id;
    jfx_project_kind_t document;
} jfx_plugin_action_info_t;

/* All registrations occur during initialize, with capability checks and
 * automatic rollback/cleanup. The table and context remain valid until
 * shutdown. Registration lifecycle and event publishing use the owner thread,
 * serialized against document operations; do not change subscriptions or
 * load/unload plugins inside callbacks. Events are borrowed for the call. */
struct jfx_plugin_api {
    size_t size;
    uint32_t sdk_major, sdk_minor;
    void *context;
    void *(*allocate)(void *context,size_t bytes,size_t alignment);
    void (*deallocate)(void *context,void *pointer);
    void (*log)(void *context,const char *message);
    jfx_result_t (*register_effect)(void *context,const jfx_plugin_effect_desc_t *desc);
    jfx_result_t (*register_kernel)(void *context,const jfx_plugin_kernel_desc_t *desc);
    jfx_result_t (*register_action)(void *context,const jfx_plugin_action_desc_t *desc);
    jfx_result_t (*subscribe)(void *context,jfx_event_type_t type,jfx_event_handler_t handler,void *userdata);
    jfx_result_t (*editor_command)(jfx_editor_t *editor,const char *op,uint32_t a,uint32_t b,uint32_t c,double value,const char *text);
    jfx_project_kind_t (*editor_kind)(const jfx_editor_t *editor);
    jfx_result_t (*sequence_state)(const jfx_editor_t *editor,char *out_json,size_t capacity);
    jfx_result_t (*graph_state)(const jfx_editor_t *editor,char *out_json,size_t capacity);
};
typedef jfx_result_t (*jfx_plugin_initialize_fn)(const jfx_plugin_api_t *api,void **out_userdata);
typedef void (*jfx_plugin_finalize_fn)(void *userdata);
typedef struct {
    size_t size;
    uint32_t sdk_major, minimum_sdk_minor;
    jfx_plugin_desc_t info;
    jfx_plugin_initialize_fn initialize;
    jfx_plugin_finalize_fn shutdown; /* Also called after failed initialization. */
} jfx_plugin_definition_t;
typedef jfx_result_t (*jfx_plugin_entry_fn)(uint32_t sdk_major,uint32_t sdk_minor,jfx_plugin_definition_t *out_definition);
/* Prior C linkage also makes definitions in C++ modules unmangled. Export the
 * definition with JFX_PLUGIN_EXPORT; use only api services for engine calls. */
jfx_result_t jfx_plugin_entry(uint32_t sdk_major,uint32_t sdk_minor,jfx_plugin_definition_t *out_definition);

jfx_result_t jfx_plugin_host_attach(jfx_plugin_host_t *host,const jfx_plugin_definition_t *definition,uint32_t *out_plugin_id);
jfx_result_t jfx_plugin_host_info_at(const jfx_plugin_host_t *host,uint32_t index,uint32_t *out_plugin_id,jfx_plugin_info_t *out_info);
uint32_t jfx_plugin_host_action_count(const jfx_plugin_host_t *host);
jfx_result_t jfx_plugin_host_action_info(const jfx_plugin_host_t *host,uint32_t index,jfx_plugin_action_info_t *out_info);
jfx_result_t jfx_plugin_host_invoke(jfx_plugin_host_t *host,const char *name,const jfx_plugin_action_context_t *context);
/* Last loader/compiler/action diagnostic; valid until the next host operation. */
const char *jfx_plugin_host_error(const jfx_plugin_host_t *host);

#ifdef __cplusplus
}
#endif
#endif
