#include "jfx/host_plugin.h"

jfx_result_t jfx_ae_plugin_get_info(jfx_host_plugin_info_t *out_info) {
    return jfx_host_plugin_get_info(JFX_HOST_AFTER_EFFECTS, out_info);
}
