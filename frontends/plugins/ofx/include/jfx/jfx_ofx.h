#ifndef JFX_OFX_H
#define JFX_OFX_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_result.h"

#ifdef __cplusplus
extern "C" {
#endif

/* OpenFX (OFX) host adapter, API 1.0.
 *
 * This is the host side of the OpenFX 1.5 image-effect API: it discovers OFX
 * bundles, drives the OFX action lifecycle, implements the property, parameter
 * and image-effect suites, and hands plugin-rendered frames back as plain
 * float RGBA. It is deliberately independent of any OpenFX host SDK and of the
 * proprietary After Effects / Premiere / DaVinci adapters in this tree; the
 * shared engine paths are reused instead.
 *
 * Threads: none. A host, its plugins and their instances are owned by the
 * calling thread and are not internally synchronised, matching the rest of the
 * host-bridge code in this repository.
 */

/* Increment MAJOR on an incompatible change to any symbol below; MINOR when
 * adding symbols or fields; PATCH for fixes. */
#define JFX_OFX_API_MAJOR 1
#define JFX_OFX_API_MINOR 0
#define JFX_OFX_API_PATCH 0

/* Fixed token limits, matching the OFX 1.5 header guidance of keeping
 * identifiers and names short and printable. */
#define JFX_OFX_MAX_ID_LENGTH 95u
#define JFX_OFX_MAX_NAME_LENGTH 95u
#define JFX_OFX_MAX_PATH_LENGTH 1023u

typedef struct jfx_ofx_host jfx_ofx_host_t;
typedef struct jfx_ofx_instance jfx_ofx_instance_t;

/* OFX image-effect contexts. These mirror the kOfxImageEffectContext* strings
 * in ofxImageEffect.h; only the CPU-oriented contexts are offered. */
typedef enum {
    JFX_OFX_CONTEXT_FILTER = 0,
    JFX_OFX_CONTEXT_GENERATOR = 1,
    JFX_OFX_CONTEXT_TRANSITION = 2,
    JFX_OFX_CONTEXT_PAINT = 3,
    JFX_OFX_CONTEXT_RETIMER = 4,
    JFX_OFX_CONTEXT_GENERAL = 5
} jfx_ofx_context_t;

/* Pixels are exchanged as tightly packed float RGBA, which is the format the
 * engine's image executor already uses. Bit depth and component count of the
 * buffers seen by the plugin are negotiated per instance and reported through
 * jfx_ofx_instance_capabilities(). */
typedef struct {
    size_t size;
    /* Directory to scan for *.ofx.bundle subdirectories. NULL selects the
     * platform default (/usr/OFX/Plugins, /Library/OFX/Plugins, or
     * %COMMONPROGRAMFILES%/OFX/Plugins). */
    const char *bundle_directory;
    /* Upper bound on instances created per plugin; 0 selects the default. */
    uint32_t max_instances_per_plugin;
} jfx_ofx_host_desc_t;

typedef struct {
    size_t size;
    char identifier[JFX_OFX_MAX_ID_LENGTH + 1u];
    char name[JFX_OFX_MAX_NAME_LENGTH + 1u];
    uint32_t version_major;
    uint32_t version_minor;
    size_t parameter_count;
    uint32_t supported_contexts; /* Bitmask of 1u << jfx_ofx_context_t. */
    char bundle_path[JFX_OFX_MAX_PATH_LENGTH + 1u];
} jfx_ofx_plugin_info_t;

typedef struct {
    size_t size;
    jfx_ofx_context_t context; /* Must be one the plugin declared. */
} jfx_ofx_instance_desc_t;

/* Parameter types, mapped onto the OFX parameter types this host defines.
 * Fetched values are always doubles; booleans are 0.0/1.0. */
typedef enum {
    JFX_OFX_PARAM_DOUBLE = 0,
    JFX_OFX_PARAM_INTEGER = 1,
    JFX_OFX_PARAM_BOOLEAN = 2,
    JFX_OFX_PARAM_COLOR = 3,   /* 3 components; set all three via set_components */
    JFX_OFX_PARAM_POINT = 4,   /* 2 components */
    JFX_OFX_PARAM_RGBA = 5,    /* 4 components */
    JFX_OFX_PARAM_STRING = 6,
    JFX_OFX_PARAM_CHOICE = 7
} jfx_ofx_param_type_t;

typedef struct {
    size_t size;
    char name[JFX_OFX_MAX_ID_LENGTH + 1u];
    char label[JFX_OFX_MAX_NAME_LENGTH + 1u];
    jfx_ofx_param_type_t type;
    double default_value;
    double minimum;
    double maximum;
    int component_count; /* 0 for scalar parameters, else 2, 3 or 4. */
    int animatable;
} jfx_ofx_param_info_t;

typedef struct {
    size_t size;
    /* Set by the host after the describe phase; not an input. */
    jfx_ofx_context_t context;
    uint32_t supports_multi_resolution;
    uint32_t supports_tiles;
    uint32_t temporal_clip_access;
    uint32_t supports_overlays;
} jfx_ofx_instance_capabilities_t;

typedef struct {
    size_t size;
    const float *src; /* NULL for a generator context. */
    size_t width;
    size_t height;
    double time_seconds;
    float *dst; /* Tightly packed float RGBA, width*height*4. */
} jfx_ofx_render_desc_t;

/* Host lifecycle. Scanning is explicit and repeatable; the host does not touch
 * the filesystem outside jfx_ofx_host_scan(). */
jfx_result_t jfx_ofx_host_create(const jfx_ofx_host_desc_t *desc,
    jfx_ofx_host_t **out_host);
void jfx_ofx_host_destroy(jfx_ofx_host_t *host);

/* Adds every OFX binary found under `directory` (non-recursively, one level of
 * *.ofx.bundle/Contents/<platform>/). Returns JFX_ERROR_NOT_FOUND when the
 * directory is absent, which is not an error for an optional plugin path. */
jfx_result_t jfx_ofx_host_scan(jfx_ofx_host_t *host, const char *directory);

size_t jfx_ofx_host_plugin_count(const jfx_ofx_host_t *host);
jfx_result_t jfx_ofx_host_plugin_info(const jfx_ofx_host_t *host, size_t index,
    jfx_ofx_plugin_info_t *out_info);

/* Instances. An instance owns one plugin's describe-in-context parameter set and
 * its clips. Creating an instance runs the OFX describe and create-instance
 * actions; destroying it runs the destroy action. */
jfx_result_t jfx_ofx_instance_create(jfx_ofx_host_t *host, size_t plugin_index,
    const jfx_ofx_instance_desc_t *desc, jfx_ofx_instance_t **out_instance);
void jfx_ofx_instance_destroy(jfx_ofx_instance_t *instance);

jfx_result_t jfx_ofx_instance_capabilities(const jfx_ofx_instance_t *instance,
    jfx_ofx_instance_capabilities_t *out_capabilities);

size_t jfx_ofx_instance_param_count(const jfx_ofx_instance_t *instance);
jfx_result_t jfx_ofx_instance_param_info(const jfx_ofx_instance_t *instance,
    size_t index, jfx_ofx_param_info_t *out_info);

/* Scalar access; multi-component parameters use the _components calls. Values
 * are clamped to the parameter's declared range, matching the OFX contract. */
jfx_result_t jfx_ofx_instance_set_param(jfx_ofx_instance_t *instance,
    const char *name, double value);
jfx_result_t jfx_ofx_instance_get_param(const jfx_ofx_instance_t *instance,
    const char *name, double *out_value);
jfx_result_t jfx_ofx_instance_set_param_components(jfx_ofx_instance_t *instance,
    const char *name, const double *values, int count);
jfx_result_t jfx_ofx_instance_get_param_components(
    const jfx_ofx_instance_t *instance, const char *name, double *out_values,
    int capacity, int *out_count);

/* Renders one frame through the OFX action pipeline. The destination is left
 * untouched when the plugin fails or aborts. */
jfx_result_t jfx_ofx_instance_render(jfx_ofx_instance_t *instance,
    const jfx_ofx_render_desc_t *desc);

/* Reports the last plugin message of severity error, or NULL. */
const char *jfx_ofx_instance_last_error(const jfx_ofx_instance_t *instance);

#ifdef __cplusplus
}
#endif

#endif /* JFX_OFX_H */