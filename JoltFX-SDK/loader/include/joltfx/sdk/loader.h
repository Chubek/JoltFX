#ifndef JOLTFX_SDK_LOADER_H
#define JOLTFX_SDK_LOADER_H
#include <joltfx/sdk/renderer.h>
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
#ifdef __cplusplus
}
#endif
#endif
