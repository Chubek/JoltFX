#include "jfx/jfx_plugin.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "jfx/jfx_events.h"
#include "tilly/allocator.h"

#if defined(_WIN32)
#include <windows.h>
typedef HMODULE jfx_module_t;
static jfx_module_t module_open(const char *path) { return LoadLibraryA(path); }
static void module_close(jfx_module_t module) { if (module) FreeLibrary(module); }
#else
#include <dlfcn.h>
typedef void *jfx_module_t;
static jfx_module_t module_open(const char *path) { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
static void *module_symbol(jfx_module_t module, const char *name) {
    return module ? dlsym(module, name) : NULL;
}
static void module_close(jfx_module_t module) { if (module) dlclose(module); }
#endif

#define JFX_MAX_PLUGINS 32u

typedef struct {
    uint32_t id;
    jfx_module_t module;
    jfx_plugin_info_t info;
    jfx_plugin_shutdown_fn shutdown;
    bool registered;
} jfx_loaded_plugin_t;

struct jfx_plugin_host {
    jfx_engine_t *engine;
    jfx_loaded_plugin_t plugins[JFX_MAX_PLUGINS];
    uint32_t count;
    uint32_t next_id;
    int32_t loading_index;
};

static bool text_fits(const char *text, size_t capacity) {
    return text && text[0] && strlen(text) < capacity;
}

static jfx_loaded_plugin_t *find_plugin(jfx_plugin_host_t *host, uint32_t id) {
    if (!host || !id) return NULL;
    for (uint32_t i = 0; i < host->count; ++i) {
        if (host->plugins[i].id == id) return &host->plugins[i];
    }
    return NULL;
}

static jfx_plugin_init_fn module_init_symbol(jfx_module_t module) {
#if defined(_WIN32)
    return (jfx_plugin_init_fn)GetProcAddress(module, "jfx_plugin_init");
#else
    void *symbol = module_symbol(module, "jfx_plugin_init");
    jfx_plugin_init_fn function = NULL;
    if (symbol && sizeof(function) == sizeof(symbol)) {
        memcpy(&function, &symbol, sizeof(function));
    }
    return function;
#endif
}

static jfx_plugin_shutdown_fn module_shutdown_symbol(jfx_module_t module) {
#if defined(_WIN32)
    return (jfx_plugin_shutdown_fn)GetProcAddress(module, "jfx_plugin_shutdown");
#else
    void *symbol = module_symbol(module, "jfx_plugin_shutdown");
    jfx_plugin_shutdown_fn function = NULL;
    if (symbol && sizeof(function) == sizeof(symbol)) {
        memcpy(&function, &symbol, sizeof(function));
    }
    return function;
#endif
}

jfx_result_t jfx_plugin_host_create(jfx_engine_t *engine,
    jfx_plugin_host_t **out_host) {
    if (!engine || !out_host) return JFX_ERROR_INVALID_ARGUMENT;
    *out_host = NULL;
    jfx_plugin_host_t *host = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*host), _Alignof(jfx_plugin_host_t));
    if (!host) return JFX_ERROR_OUT_OF_MEMORY;
    memset(host, 0, sizeof(*host));
    host->engine = engine;
    host->next_id = 1;
    host->loading_index = -1;
    *out_host = host;
    return JFX_SUCCESS;
}

void jfx_plugin_host_destroy(jfx_plugin_host_t *host) {
    if (!host) return;
    while (host->count > 0) {
        (void)jfx_plugin_host_unload(host, host->plugins[host->count - 1].id);
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), host);
}

jfx_result_t jfx_plugin_host_register(jfx_plugin_host_t *host,
    const jfx_plugin_desc_t *desc) {
    if (!host || !desc || desc->size < sizeof(*desc) || host->loading_index < 0 ||
        (uint32_t)host->loading_index >= host->count ||
        !text_fits(desc->identifier, JFX_PLUGIN_MAX_ID_LENGTH + 1u) ||
        !text_fits(desc->display_name, JFX_PLUGIN_MAX_NAME_LENGTH + 1u) ||
        !text_fits(desc->vendor, JFX_PLUGIN_MAX_NAME_LENGTH + 1u)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (desc->minimum_api_version > JFX_PLUGIN_API_VERSION) {
        return JFX_ERROR_VERSION_MISMATCH;
    }
    jfx_loaded_plugin_t *slot = &host->plugins[host->loading_index];
    if (slot->registered) return JFX_ERROR_ALREADY_EXISTS;
    for (uint32_t i = 0; i + 1 < host->count; ++i) {
        if (strcmp(host->plugins[i].info.identifier, desc->identifier) == 0) {
            return JFX_ERROR_ALREADY_EXISTS;
        }
    }
    slot->info.size = sizeof(slot->info);
    snprintf(slot->info.identifier, sizeof(slot->info.identifier), "%s", desc->identifier);
    snprintf(slot->info.display_name, sizeof(slot->info.display_name), "%s", desc->display_name);
    snprintf(slot->info.vendor, sizeof(slot->info.vendor), "%s", desc->vendor);
    slot->info.version = desc->version;
    slot->info.minimum_api_version = desc->minimum_api_version;
    slot->info.capabilities = desc->capabilities;
    slot->registered = true;
    return JFX_SUCCESS;
}

jfx_result_t jfx_plugin_host_load(jfx_plugin_host_t *host, const char *path,
    uint32_t *out_plugin_id) {
    if (!host || !path || !path[0] || !out_plugin_id) return JFX_ERROR_INVALID_ARGUMENT;
    *out_plugin_id = 0;
    if (host->count == JFX_MAX_PLUGINS || host->loading_index >= 0) return JFX_ERROR_OUT_OF_MEMORY;
    jfx_module_t module = module_open(path);
    if (!module) return JFX_ERROR_NOT_FOUND;
    jfx_plugin_init_fn initialize = module_init_symbol(module);
    if (!initialize) { module_close(module); return JFX_ERROR_NOT_FOUND; }
    uint32_t index = host->count++;
    jfx_loaded_plugin_t *slot = &host->plugins[index];
    memset(slot, 0, sizeof(*slot));
    slot->id = host->next_id++;
    if (!slot->id) slot->id = host->next_id++;
    slot->module = module;
    slot->shutdown = module_shutdown_symbol(module);
    host->loading_index = (int32_t)index;
    jfx_result_t result = initialize(host, JFX_PLUGIN_API_VERSION);
    host->loading_index = -1;
    if (result != JFX_SUCCESS || !slot->registered) {
        if (slot->shutdown) slot->shutdown(host);
        module_close(module);
        --host->count;
        return result == JFX_SUCCESS ? JFX_ERROR_PLUGIN_FAILURE : result;
    }
    event_publish(JFX_EVENT_PLUGIN_LOAD, &slot->info);
    *out_plugin_id = slot->id;
    return JFX_SUCCESS;
}

jfx_result_t jfx_plugin_host_unload(jfx_plugin_host_t *host, uint32_t plugin_id) {
    jfx_loaded_plugin_t *slot = find_plugin(host, plugin_id);
    if (!slot) return JFX_ERROR_NOT_FOUND;
    event_publish(JFX_EVENT_PLUGIN_UNLOAD, &slot->info);
    if (slot->shutdown) slot->shutdown(host);
    module_close(slot->module);
    uint32_t index = (uint32_t)(slot - host->plugins);
    if (index + 1 < host->count) {
        memmove(slot, slot + 1, (host->count - index - 1) * sizeof(*slot));
    }
    --host->count;
    memset(&host->plugins[host->count], 0, sizeof(host->plugins[host->count]));
    return JFX_SUCCESS;
}

uint32_t jfx_plugin_host_count(const jfx_plugin_host_t *host) {
    return host ? host->count : 0;
}

jfx_result_t jfx_plugin_host_get_info(const jfx_plugin_host_t *host,
    uint32_t plugin_id, jfx_plugin_info_t *out_info) {
    if (!host || !out_info || out_info->size < sizeof(*out_info)) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_loaded_plugin_t *slot = find_plugin((jfx_plugin_host_t *)host, plugin_id);
    if (!slot) return JFX_ERROR_NOT_FOUND;
    *out_info = slot->info;
    return JFX_SUCCESS;
}
