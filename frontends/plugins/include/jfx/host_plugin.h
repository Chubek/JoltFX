#ifndef JFX_HOST_PLUGIN_H
#define JFX_HOST_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JFX_HOST_AFTER_EFFECTS = 0,
    JFX_HOST_PREMIERE,
    JFX_HOST_DAVINCI,
    JFX_HOST_COUNT
} jfx_host_kind_t;

typedef enum {
    JFX_HOST_PLUGIN_IMPORT = 1u << 0,
    JFX_HOST_PLUGIN_EXPORT = 1u << 1,
    JFX_HOST_PLUGIN_EFFECT = 1u << 2
} jfx_host_plugin_feature_t;

typedef struct {
    size_t size;
    jfx_host_kind_t host;
    const char *host_name;
    const char *plugin_identifier;
    uint32_t features;
    uint32_t requires_host_sdk;
} jfx_host_plugin_info_t;

/* Describes the SDK-independent bridge. Host SDK entry points are optional. */
jfx_result_t jfx_host_plugin_get_info(jfx_host_kind_t host,
    jfx_host_plugin_info_t *out_info);

jfx_result_t jfx_ae_plugin_get_info(jfx_host_plugin_info_t *out_info);
jfx_result_t jfx_premiere_plugin_get_info(jfx_host_plugin_info_t *out_info);
jfx_result_t jfx_davinci_plugin_get_info(jfx_host_plugin_info_t *out_info);

#ifdef __cplusplus
}
#endif

#endif /* JFX_HOST_PLUGIN_H */
