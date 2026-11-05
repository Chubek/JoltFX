/* DESIGN DRAFT ONLY: not installed or implemented. Retains the proposed full
 * execution contract for the next milestone; see FUTURE_OF_SDK.md. */
#ifndef JOLTFX_SDK_OPENFX_H
#define JOLTFX_SDK_OPENFX_H
/* Minimal OpenFX 1.4 image-effect hosting over the vendored OpenFX headers.
 * The host supports the filter context with tightly packed straight RGBA8
 * full-frame images, non-animated double/integer/boolean parameters and
 * synchronous single-threaded rendering. Overlays, tiles, temporal access,
 * GPU rendering, interacts and keyframes are not provided. OpenFX binaries
 * are trusted native code, not sandboxed. Native loading is unavailable on
 * WASM hosts. One owner thread per host; calls are serialized. */
#include <jfx/jfx_result.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JFX_OFX_HOST_API_MAJOR 1u
#define JFX_OFX_HOST_API_MINOR 0u
#define JFX_OFX_MAX_PLUGINS 32u
#define JFX_OFX_MAX_PARAMS 32u
#define JFX_OFX_MAX_DIMENSION 4096u

typedef struct jfx_ofx_host jfx_ofx_host_t;
typedef struct jfx_ofx_effect jfx_ofx_effect_t;

/* The caller owns context and allocation lifetime; allocations are released
 * through this SAME table. log is optional and borrows the message. */
typedef struct {
    size_t size;
    void *context;
    void *(*allocate)(void *context, size_t bytes, size_t alignment);
    void (*release)(void *context, void *pointer);
    void (*log)(void *context, const char *message);
} jfx_ofx_services_t;

typedef enum {
    JFX_OFX_PARAM_DOUBLE = 0,
    JFX_OFX_PARAM_INTEGER = 1,
    JFX_OFX_PARAM_BOOLEAN = 2,
    /* Declared but unsupported parameter kinds (pages, groups, strings, ...)
     * are reported for inspection and hold no value. */
    JFX_OFX_PARAM_OTHER = 3
} jfx_ofx_param_type_t;

typedef struct {
    size_t size;
    char name[64];
    char label[96];
    jfx_ofx_param_type_t type;
    double default_value, minimum, maximum;
    bool secret;
} jfx_ofx_param_info_t;

typedef struct {
    size_t size;
    char identifier[96];
    char label[96];
    char grouping[96];
    uint32_t version_major, version_minor;
    /* True when the plugin advertises the filter context with RGBA byte
     * support; only such plugins can become effect instances here. */
    bool supports_filter;
} jfx_ofx_plugin_info_t;

/* Loads an OpenFX binary (an .ofx file or a platform module inside a bundle)
 * and negotiates OfxSetHost/OfxGetNumberOfPlugins/OfxGetPlugin. The services
 * table is copied; its context must outlive the host. NOT_FOUND covers both
 * a missing file and missing OpenFX entry symbols. */
jfx_result_t jfx_ofx_host_open(const char *path, const jfx_ofx_services_t *services,
    jfx_ofx_host_t **out_host);
/* Unloads loaded plugins and the binary. Open effects hold the host: BUSY. */
jfx_result_t jfx_ofx_host_close(jfx_ofx_host_t *host);
uint32_t jfx_ofx_host_plugin_count(const jfx_ofx_host_t *host);
/* Runs the plugin's load/describe actions on first use and reports the
 * described metadata. out_info->size must be initialized. */
jfx_result_t jfx_ofx_host_plugin_info(jfx_ofx_host_t *host, uint32_t plugin,
    jfx_ofx_plugin_info_t *out_info);
/* Last OpenFX diagnostic; valid until the next call on the same host. */
const char *jfx_ofx_host_error(const jfx_ofx_host_t *host);

/* Describes the plugin in the filter context and prepares an instance.
 * Parameter values start at their declared defaults. */
jfx_result_t jfx_ofx_effect_create(jfx_ofx_host_t *host, uint32_t plugin,
    jfx_ofx_effect_t **out_effect);
/* NULL is a successful no-op. Rejects callback reentry with BUSY. */
jfx_result_t jfx_ofx_effect_destroy(jfx_ofx_effect_t *effect);
uint32_t jfx_ofx_effect_param_count(const jfx_ofx_effect_t *effect);
jfx_result_t jfx_ofx_effect_param_info(const jfx_ofx_effect_t *effect, uint32_t param,
    jfx_ofx_param_info_t *out_info);
/* Values are validated against the declared range; booleans accept 0/1 and
 * integers require whole numbers. OTHER parameters reject both directions. */
jfx_result_t jfx_ofx_effect_set_param(jfx_ofx_effect_t *effect, const char *name, double value);
jfx_result_t jfx_ofx_effect_get_param(const jfx_ofx_effect_t *effect, const char *name,
    double *out_value);
/* Synchronous full-frame filter render at `seconds`. Input and output are
 * borrowed, tightly packed straight RGBA8 buffers of width*height*4 bytes and
 * must not alias. Dimensions are 1..JFX_OFX_MAX_DIMENSION. The instance is
 * created on first render. Output is unspecified on failure. */
jfx_result_t jfx_ofx_effect_render(jfx_ofx_effect_t *effect, const uint8_t *in_rgba,
    uint8_t *out_rgba, uint32_t width, uint32_t height, double seconds);

#ifdef __cplusplus
}
#endif
#endif
