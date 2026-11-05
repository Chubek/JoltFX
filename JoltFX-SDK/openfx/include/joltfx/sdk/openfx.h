#ifndef JOLTFX_SDK_OPENFX_H
#define JOLTFX_SDK_OPENFX_H
/* OpenFX 1.4 image-effect module inspection. No engine linkage required.
 * Native modules are trusted code. Use one serialized owner thread per host;
 * callback reentry returns BUSY. This component does not render images. */
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
typedef struct jfx_ofx_host jfx_ofx_host_t;
/* Copied at open. Context and callbacks must remain valid until close returns.
 * Allocate must honor alignment; release receives only matching allocations.
 * log is optional and borrows its message for the duration of the callback. */
typedef struct {
    size_t size;
    void *context;
    void *(*allocate)(void *context,size_t bytes,size_t alignment);
    void (*release)(void *context,void *pointer);
    void (*log)(void *context,const char *message);
} jfx_ofx_services_t;
typedef struct {
    size_t size;
    char identifier[96];
    char label[96];
    char grouping[96];
    uint32_t version_major,version_minor;
    /* Advertises filter context and byte depth; NOT a render compatibility
     * guarantee. Clip components/parameters are not negotiated by inspection. */
    bool supports_filter;
} jfx_ofx_plugin_info_t;
/* path names a native binary, not a bundle directory. Requires OfxGetPlugin
 * and OfxGetNumberOfPlugins; optional OfxSetHost precedes enumeration. Calls
 * each plugin's mandatory setHost. On failure *out_host is NULL. WASM returns
 * NOT_IMPLEMENTED. Only one open host per loaded binary is supported. */
jfx_result_t jfx_ofx_host_open(const char *path,const jfx_ofx_services_t *services,
    jfx_ofx_host_t **out_host);
/* Unload successfully loaded plugins, then close the module. NULL is a no-op. */
jfx_result_t jfx_ofx_host_close(jfx_ofx_host_t *host);
uint32_t jfx_ofx_host_plugin_count(const jfx_ofx_host_t *host);
/* Lazy Load/Describe, cached on success; failed describes unload and can retry.
 * Initialize out_info->size. Output is unchanged on failure; labels/grouping
 * are bounded copies, valid after close. Missing suites can yield NOT_IMPLEMENTED. */
jfx_result_t jfx_ofx_host_plugin_info(jfx_ofx_host_t *host,uint32_t plugin,
    jfx_ofx_plugin_info_t *out_info);
/* Borrowed diagnostic, valid until the next mutating call or close. */
const char *jfx_ofx_host_error(const jfx_ofx_host_t *host);
#ifdef __cplusplus
}
#endif
#endif
