#include "jfx/host_plugin.h"

#include <assert.h>
#include <string.h>

int main(void) {
    jfx_host_plugin_info_t info = { .size = sizeof(info) };
    assert(jfx_ae_plugin_get_info(&info) == JFX_SUCCESS);
    assert(info.host == JFX_HOST_AFTER_EFFECTS);
    assert(strcmp(info.plugin_identifier, "org.joltfx.after-effects") == 0);
    assert(info.requires_host_sdk == 1u);
    info.size = sizeof(info);
    assert(jfx_premiere_plugin_get_info(&info) == JFX_SUCCESS);
    assert(info.host == JFX_HOST_PREMIERE);
    info.size = sizeof(info);
    assert(jfx_davinci_plugin_get_info(&info) == JFX_SUCCESS);
    assert(info.host == JFX_HOST_DAVINCI);
    assert((info.features & JFX_HOST_PLUGIN_EFFECT) != 0u);
    return 0;
}
