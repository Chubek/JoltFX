#ifndef JFX_PLUGIN_INTERNAL_H
#define JFX_PLUGIN_INTERNAL_H
#include "jfx/jfx_compose.h"

/* Process-wide immutable descriptor registry, consistent with the builtin
 * catalog. Lifecycle mutation is serialized by the frontend owner thread. */
size_t jfx_plugin_kind_count(void);
const jfx_node_kind_t *jfx_plugin_kind_at(size_t index);
bool jfx_plugin_kind_retain(const jfx_node_kind_t *kind);
void jfx_plugin_kind_release(const jfx_node_kind_t *kind);
bool jfx_plugin_kind_is_custom(const jfx_node_kind_t *kind);
jfx_result_t jfx_plugin_kind_process(const jfx_node_kind_t *kind,const float *params,
    const float *input,uint32_t width,uint32_t height,double seconds,size_t limit,float *out);
#endif
