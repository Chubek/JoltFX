#include "jfx/jfx_plugin.h"

jfx_result_t jfx_plugin_init(jfx_plugin_host_t *host, uint32_t api_version) {
    if (api_version != JFX_PLUGIN_API_VERSION) return JFX_ERROR_VERSION_MISMATCH;
    const jfx_plugin_desc_t descriptor = {
        .size = sizeof(descriptor),
        .identifier = "org.joltfx.test-plugin",
        .display_name = "JoltFX Test Plugin",
        .vendor = "JoltFX QA",
        .version = 1u,
        .minimum_api_version = JFX_PLUGIN_API_VERSION,
        .capabilities = JFX_PLUGIN_CAP_EVENTS | JFX_PLUGIN_CAP_KERNELS,
    };
    return jfx_plugin_host_register(host, &descriptor);
}

void jfx_plugin_shutdown(jfx_plugin_host_t *host) { (void)host; }
