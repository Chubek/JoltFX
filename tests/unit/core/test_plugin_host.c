#include "jfx/jfx_engine.h"
#include "jfx/jfx_events.h"
#include "jfx/jfx_plugin.h"

#include <assert.h>
#include <string.h>

static int load_events;
static int unload_events;

static void event_handler(jfx_event_type_t type, void *data, void *userdata) {
    const jfx_plugin_info_t *info = data;
    (void)userdata;
    assert(info && strcmp(info->identifier, "org.joltfx.test-plugin") == 0);
    if (type == JFX_EVENT_PLUGIN_LOAD) ++load_events;
    if (type == JFX_EVENT_PLUGIN_UNLOAD) ++unload_events;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    jfx_engine_t *engine = NULL;
    jfx_engine_config_t config = { .max_buffers = 1, .backend_name = "webgpu" };
    assert(jfx_engine_init(&config, &engine) == JFX_SUCCESS);
    jfx_plugin_host_t *host = NULL;
    assert(jfx_plugin_host_create(engine, &host) == JFX_SUCCESS);
    assert(jfx_plugin_host_load(host, "does-not-exist", &(uint32_t){0}) == JFX_ERROR_NOT_FOUND);
    assert(event_subscribe(JFX_EVENT_PLUGIN_LOAD, event_handler, NULL));
    assert(event_subscribe(JFX_EVENT_PLUGIN_UNLOAD, event_handler, NULL));
    uint32_t plugin_id = 0;
    assert(jfx_plugin_host_load(host, argv[1], &plugin_id) == JFX_SUCCESS && plugin_id != 0);
    assert(jfx_plugin_host_count(host) == 1 && load_events == 1);
    jfx_plugin_info_t info = { .size = sizeof(info) };
    assert(jfx_plugin_host_get_info(host, plugin_id, &info) == JFX_SUCCESS);
    assert(info.capabilities == (JFX_PLUGIN_CAP_EVENTS | JFX_PLUGIN_CAP_KERNELS));
    assert(jfx_plugin_host_load(host, argv[1], &(uint32_t){0}) == JFX_ERROR_ALREADY_EXISTS);
    assert(jfx_plugin_host_count(host) == 1);
    assert(jfx_plugin_host_unload(host, plugin_id) == JFX_SUCCESS && unload_events == 1);
    assert(jfx_plugin_host_unload(host, plugin_id) == JFX_ERROR_NOT_FOUND);
    jfx_plugin_host_destroy(host);
    jfx_engine_shutdown(engine);
    return 0;
}
