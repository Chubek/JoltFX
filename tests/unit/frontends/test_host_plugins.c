/* Host-plugin bridge contract.
 *
 * The shipped bridges are host-independent: they identify and version
 * themselves but implement no import, export or effect registration. This
 * test pins that, so the day one is implemented the capability bit is added
 * here at the same time. */

#include "jfx/host_plugin.h"

#include <assert.h>
#include <string.h>

static void check_bridge(jfx_result_t (*get_info)(jfx_host_plugin_info_t *),
    jfx_host_kind_t expected_host, const char *expected_identifier) {
    jfx_host_plugin_info_t info = { .size = sizeof(info) };
    assert(get_info(&info) == JFX_SUCCESS);
    assert(info.host == expected_host);
    assert(info.host_name && info.host_name[0]);
    assert(strcmp(info.plugin_identifier, expected_identifier) == 0);
    assert(info.requires_host_sdk == 1u);
    /* Nothing is implemented, so nothing may be advertised. */
    assert(info.features == 0u);

    /* The size guard rejects a stale or zeroed size. */
    info.size = 0;
    assert(get_info(&info) == JFX_ERROR_INVALID_ARGUMENT);
    assert(get_info(NULL) == JFX_ERROR_INVALID_ARGUMENT);
}

int main(void) {
    check_bridge(jfx_ae_plugin_get_info, JFX_HOST_AFTER_EFFECTS, "org.joltfx.after-effects");
    check_bridge(jfx_premiere_plugin_get_info, JFX_HOST_PREMIERE, "org.joltfx.premiere");
    check_bridge(jfx_davinci_plugin_get_info, JFX_HOST_DAVINCI, "org.joltfx.davinci");

    jfx_host_plugin_info_t info = { .size = sizeof(info) };
    assert(jfx_host_plugin_get_info((jfx_host_kind_t)-1, &info) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_plugin_get_info(JFX_HOST_COUNT, &info) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_plugin_get_info(JFX_HOST_DAVINCI, &info) == JFX_SUCCESS);
    assert(info.host == JFX_HOST_DAVINCI);
    assert(strcmp(info.host_name, "DaVinci Resolve") == 0);
    return 0;
}
