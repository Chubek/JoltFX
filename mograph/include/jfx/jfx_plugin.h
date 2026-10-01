#ifndef JFX_PLUGIN_H
#define JFX_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Increment only when a source/binary incompatible plugin API is introduced. */
#define JFX_PLUGIN_API_VERSION 1u
#define JFX_PLUGIN_MAX_ID_LENGTH 63u
#define JFX_PLUGIN_MAX_NAME_LENGTH 95u

typedef struct jfx_plugin_host jfx_plugin_host_t;

typedef enum {
    JFX_PLUGIN_CAP_KERNELS = 1u << 0,
    JFX_PLUGIN_CAP_TYPES = 1u << 1,
    JFX_PLUGIN_CAP_EVENTS = 1u << 2,
    JFX_PLUGIN_CAP_ASSETS = 1u << 3,
    JFX_PLUGIN_CAP_BACKEND = 1u << 4,
    JFX_PLUGIN_CAP_IO = 1u << 5
} jfx_plugin_capability_t;

typedef struct {
    size_t size;
    const char *identifier; /* Stable reverse-DNS-style ID. */
    const char *display_name;
    const char *vendor;
    uint32_t version;
    uint32_t minimum_api_version;
    uint32_t capabilities;
} jfx_plugin_desc_t;

typedef struct {
    size_t size;
    char identifier[JFX_PLUGIN_MAX_ID_LENGTH + 1u];
    char display_name[JFX_PLUGIN_MAX_NAME_LENGTH + 1u];
    char vendor[JFX_PLUGIN_MAX_NAME_LENGTH + 1u];
    uint32_t version;
    uint32_t minimum_api_version;
    uint32_t capabilities;
} jfx_plugin_info_t;

typedef jfx_result_t (*jfx_plugin_init_fn)(jfx_plugin_host_t *host,
    uint32_t api_version);
typedef void (*jfx_plugin_shutdown_fn)(jfx_plugin_host_t *host);

/* A module exports this exact symbol. Registration happens during this call. */
jfx_result_t jfx_plugin_init(jfx_plugin_host_t *host, uint32_t api_version);

jfx_result_t jfx_plugin_host_create(jfx_engine_t *engine,
    jfx_plugin_host_t **out_host);
void jfx_plugin_host_destroy(jfx_plugin_host_t *host);

/* Load a native module, invoke jfx_plugin_init, and return a host-local ID. */
jfx_result_t jfx_plugin_host_load(jfx_plugin_host_t *host, const char *path,
    uint32_t *out_plugin_id);
jfx_result_t jfx_plugin_host_unload(jfx_plugin_host_t *host,
    uint32_t plugin_id);
uint32_t jfx_plugin_host_count(const jfx_plugin_host_t *host);
jfx_result_t jfx_plugin_host_get_info(const jfx_plugin_host_t *host,
    uint32_t plugin_id, jfx_plugin_info_t *out_info);

/* Called by a module from jfx_plugin_init exactly once. */
jfx_result_t jfx_plugin_host_register(jfx_plugin_host_t *host,
    const jfx_plugin_desc_t *desc);

#ifdef __cplusplus
}
#endif

#endif /* JFX_PLUGIN_H */
