#include "jfx/host_plugin.h"

typedef struct {
    const char *name;
    const char *identifier;
} host_definition_t;

static const host_definition_t definitions[JFX_HOST_COUNT] = {
    [JFX_HOST_AFTER_EFFECTS] = { "After Effects", "org.joltfx.after-effects" },
    [JFX_HOST_PREMIERE] = { "Premiere Pro", "org.joltfx.premiere" },
    [JFX_HOST_DAVINCI] = { "DaVinci Resolve", "org.joltfx.davinci" },
};

jfx_result_t jfx_host_plugin_get_info(jfx_host_kind_t host,
    jfx_host_plugin_info_t *out_info) {
    if (!out_info || out_info->size < sizeof(*out_info) || host < 0 || host >= JFX_HOST_COUNT) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    out_info->size = sizeof(*out_info);
    out_info->host = host;
    out_info->host_name = definitions[host].name;
    out_info->plugin_identifier = definitions[host].identifier;
    /* Advertise nothing. This translation unit ships the host-independent
     * bridge only: no host SDK entry point, no AEGP/Fusion/OpenFX registration
     * and no import or export is implemented, so claiming those features would
     * make a host accept a plugin that cannot do the job. Features are added
     * here by the same file that implements them. */
    out_info->features = 0u;
    /* The proprietary host SDK (AE SDK, Premiere SDK, Fusion/OpenFX) is required
     * before any of these bridges can be loaded into a host. */
    out_info->requires_host_sdk = 1u;
    return JFX_SUCCESS;
}
