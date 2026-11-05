#ifndef JOLTFX_SDK_LOADER_H
#define JOLTFX_SDK_LOADER_H
#include <joltfx/sdk/renderer.h>
#include <jfx/jfx_plugin_sdk.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jfx_renderer_module jfx_renderer_module_t;
/* Single owner thread, serialized lifecycle/render. The host table is copied;
 * its context must outlive the module. Output handles are NULL on failure.
 * Native loading is unavailable on WASM; use attach for static modules. */
jfx_result_t jfx_renderer_module_open(const char *path,const jfx_renderer_host_t *host,
    jfx_renderer_module_t **out_module);
jfx_result_t jfx_renderer_module_attach(const jfx_renderer_definition_t *definition,
    const jfx_renderer_host_t *host,jfx_renderer_module_t **out_module);
jfx_result_t jfx_renderer_module_info(const jfx_renderer_module_t *module,
    jfx_renderer_definition_t *out_definition);
jfx_result_t jfx_renderer_module_render(jfx_renderer_module_t *module,
    const jfx_renderer_frame_t *frame,uint8_t *out_rgba,size_t capacity);
/* Rejects callback reentry with BUSY. NULL is a successful no-op. Definition
 * pointers/info become invalid after close. No module pointer may escape into
 * an asynchronous job; close only after that job has completed. */
jfx_result_t jfx_renderer_module_close(jfx_renderer_module_t *module);
/* Owned copy of a renderer definition's metadata; valid after module close. */
typedef struct {
    size_t size;
    uint32_t abi_major,minimum_abi_minor;
    char identifier[96],name[96];
    uint32_t version,capabilities;
} jfx_renderer_info_t;
/* Standalone inspection without host services or engine linkage. Each peek
 * loads the module, negotiates its entry point, copies the metadata into the
 * caller's struct (size must be initialized) and closes the module before
 * returning. Unavailable on WASM, like native loading. NOT_FOUND means the
 * module or its entry symbol is missing; VERSION_MISMATCH means the module
 * targets an incompatible ABI/SDK major version. */
jfx_result_t jfx_renderer_module_peek(const char *path,jfx_renderer_info_t *out_info);
jfx_result_t jfx_plugin_module_peek(const char *path,jfx_plugin_info_t *out_info);
#ifdef __cplusplus
}
#endif
#endif
