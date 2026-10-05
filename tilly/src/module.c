#include "runtime_internal.h"
#include <string.h>
#include <dlfcn.h>

static void remove_entry(tilly_context_t *ctx, tilly_module_entry_t *entry) {
    for (uint32_t i = 0; i < ctx->module_count; ++i) {
        if (ctx->modules[i] == entry) {
            memmove(ctx->modules + i, ctx->modules + i + 1,
                (ctx->module_count - i - 1) * sizeof(*ctx->modules));
            --ctx->module_count;
            ctx->modules[ctx->module_count] = NULL;
            break;
        }
    }
}
static void free_entry(tilly_context_t *ctx, tilly_module_entry_t *entry) {
    tilly_free(ctx->heap, (void *)entry->module.name);
    tilly_free(ctx->heap, entry->path);
    tilly_free(ctx->heap, entry);
}

tilly_module_t *tilly_module_load(tilly_context_t *ctx, const char *path) {
    if (!ctx || !path || !*path) return NULL;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    if (ctx->shutting_down) { pthread_mutex_unlock(&ctx->lifecycle_lock); return NULL; }
    pthread_mutex_lock(&ctx->module_lock);
    for (uint32_t i = 0; i < ctx->module_count; ++i) {
        tilly_module_entry_t *entry = ctx->modules[i];
        if (strcmp(entry->path, path) == 0) {
            tilly_module_t *mod = NULL;
            /* An initializing entry is a cyclic dependency, not a usable
             * module. Prevent reference-count wraparound as well. */
            if (!entry->initializing && entry->module.ref_count < UINT32_MAX) {
                ++entry->module.ref_count;
                mod = &entry->module;
            }
            pthread_mutex_unlock(&ctx->module_lock);
            pthread_mutex_unlock(&ctx->lifecycle_lock);
            return mod;
        }
    }
    pthread_mutex_unlock(&ctx->module_lock);

    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        tilly_log_error("module", "Failed to load module %s: %s", path, dlerror());
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return NULL;
    }
    /* Different path spellings can resolve to the same native library. The
     * extra dlopen reference is balanced even when reusing an existing entry. */
    pthread_mutex_lock(&ctx->module_lock);
    for (uint32_t i = 0; i < ctx->module_count; ++i) {
        tilly_module_entry_t *entry = ctx->modules[i];
        if (entry->module.handle == handle) {
            tilly_module_t *mod = NULL;
            if (!entry->initializing && entry->module.ref_count < UINT32_MAX) {
                ++entry->module.ref_count;
                mod = &entry->module;
            }
            pthread_mutex_unlock(&ctx->module_lock);
            dlclose(handle);
            pthread_mutex_unlock(&ctx->lifecycle_lock);
            return mod;
        }
    }
    bool full = ctx->module_count == TILLY_MAX_MODULES;
    pthread_mutex_unlock(&ctx->module_lock);
    if (full) { dlclose(handle); pthread_mutex_unlock(&ctx->lifecycle_lock); return NULL; }
    union { void *object; tilly_module_api_t *(*function)(void); } symbol;
    symbol.object = dlsym(handle, "tilly_module_register");
    tilly_module_api_t *api = symbol.function ? symbol.function() : NULL;
    if (!api) {
        dlclose(handle);
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return NULL;
    }
    tilly_module_entry_t *entry = tilly_alloc(ctx->heap, sizeof(*entry), _Alignof(tilly_module_entry_t));
    if (!entry) { dlclose(handle); pthread_mutex_unlock(&ctx->lifecycle_lock); return NULL; }
    memset(entry, 0, sizeof(*entry));
    entry->owner = ctx;
    entry->path = tilly_alloc(ctx->heap, strlen(path) + 1, 1);
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *dot = strrchr(name, '.');
    size_t length = dot ? (size_t)(dot - name) : strlen(name);
    char *display_name = tilly_alloc(ctx->heap, length + 1, 1);
    entry->module.name = display_name;
    if (!entry->path || !display_name) {
        free_entry(ctx, entry); dlclose(handle);
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return NULL;
    }
    strcpy(entry->path, path);
    memcpy(display_name, name, length); display_name[length] = '\0';
    entry->module.version = api->version ? api->version : "1.0";
    entry->module.handle = handle;
    entry->module.api = api;
    entry->module.ref_count = 1;
    entry->initializing = true;
    pthread_mutex_lock(&ctx->module_lock);
    /* A registration callback can load dependencies before this entry exists. */
    if (ctx->module_count == TILLY_MAX_MODULES) {
        pthread_mutex_unlock(&ctx->module_lock);
        free_entry(ctx, entry); dlclose(handle);
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return NULL;
    }
    ctx->modules[ctx->module_count++] = entry;
    pthread_mutex_unlock(&ctx->module_lock);

    int result = api->init ? api->init(ctx) : 0;
    pthread_mutex_lock(&ctx->module_lock);
    remove_entry(ctx, entry);
    if (!result) {
        /* Dependencies initialized during the callback must outlive this
         * module. Preserve completion order for reverse-order shutdown. */
        entry->initializing = false;
        ctx->modules[ctx->module_count++] = entry;
    }
    pthread_mutex_unlock(&ctx->module_lock);
    if (result) {
        tilly_log_error("module", "Module %s init failed: %d", display_name, result);
        free_entry(ctx, entry); dlclose(handle);
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return NULL;
    }
    tilly_log_info("module", "Loaded module: %s v%s", display_name, entry->module.version);
    pthread_mutex_unlock(&ctx->lifecycle_lock);
    return &entry->module;
}

void tilly_module_unload(tilly_context_t *ctx, tilly_module_t *mod) {
    if (!ctx || !mod) return;
    pthread_mutex_lock(&ctx->lifecycle_lock);
    pthread_mutex_lock(&ctx->module_lock);
    tilly_module_entry_t *entry = NULL;
    for (uint32_t i = 0; i < ctx->module_count; ++i)
        if (&ctx->modules[i]->module == mod) { entry = ctx->modules[i]; break; }
    /* Check ownership before touching the supplied handle. */
    if (!entry || entry->initializing) {
        pthread_mutex_unlock(&ctx->module_lock);
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return;
    }
    if (mod->ref_count > 1) {
        --mod->ref_count;
        pthread_mutex_unlock(&ctx->module_lock);
        pthread_mutex_unlock(&ctx->lifecycle_lock);
        return;
    }
    remove_entry(ctx, entry);
    pthread_mutex_unlock(&ctx->module_lock);
    if (mod->api && mod->api->shutdown) mod->api->shutdown(ctx);
    if (mod->handle) dlclose(mod->handle);
    free_entry(ctx, entry);
    tilly_log_info("module", "Unloaded module");
    pthread_mutex_unlock(&ctx->lifecycle_lock);
}

tilly_module_t *tilly_module_find(tilly_context_t *ctx, const char *name) {
    if (!ctx || !name) return NULL;
    pthread_mutex_lock(&ctx->module_lock);
    tilly_module_t *mod = NULL;
    for (uint32_t i = 0; i < ctx->module_count; ++i) {
        tilly_module_entry_t *entry = ctx->modules[i];
        if (!entry->initializing && strcmp(entry->module.name, name) == 0) {
            mod = &entry->module; break;
        }
    }
    pthread_mutex_unlock(&ctx->module_lock);
    return mod;
}
void *tilly_module_get_symbol(tilly_module_t *mod, const char *symbol) {
    return mod && mod->handle && symbol ? dlsym(mod->handle, symbol) : NULL;
}
tilly_context_t *tilly_module_get_context(tilly_module_t *mod) {
    return mod ? ((tilly_module_entry_t *)(void *)mod)->owner : NULL;
}
