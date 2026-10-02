#include "tilly/module.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "tillyz/tillyz.h"
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include <pthread.h>

#define MAX_MODULES 32

// Forward declaration of context
struct tilly_context {
    tillyz_context_t *bootstrap_ctx;
    tilly_allocator_t *heap;
    tilly_module_t *modules[MAX_MODULES];
    uint32_t module_count;
    pthread_mutex_t module_lock;
};

static struct tilly_context *g_context = NULL;

tilly_module_t *tilly_module_load(struct tilly_context *ctx, const char *path) {
    if (!ctx || !path) return NULL;
    
    pthread_mutex_lock(&ctx->module_lock);
    
    // Check if already loaded
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (ctx->modules[i] && strcmp(ctx->modules[i]->name, path) == 0) {
            ctx->modules[i]->ref_count++;
            pthread_mutex_unlock(&ctx->module_lock);
            return ctx->modules[i];
        }
    }
    
    if (ctx->module_count >= MAX_MODULES) {
        pthread_mutex_unlock(&ctx->module_lock);
        return NULL;
    }
    
    // Load dynamic library
    void *handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        tilly_log_error("module", "Failed to load module %s: %s", path, dlerror());
        pthread_mutex_unlock(&ctx->module_lock);
        return NULL;
    }
    
    // Get the registration function. ISO C has no implicit conversion from
    // void* to a function pointer, so the round trip goes through a union
    // rather than a cast that -Wpedantic rejects.
    union { void *object; tilly_module_api_t *(*function)(void); } symbol;
    symbol.object = dlsym(handle, "tilly_module_register");
    tilly_module_api_t *(*register_fn)(void) = symbol.function;
    if (!register_fn) {
        dlclose(handle);
        tilly_log_error("module", "Module %s missing tilly_module_register", path);
        pthread_mutex_unlock(&ctx->module_lock);
        return NULL;
    }
    
    tilly_module_api_t *api = register_fn();
    if (!api) {
        dlclose(handle);
        pthread_mutex_unlock(&ctx->module_lock);
        return NULL;
    }
    
    // Allocate module struct
    tilly_module_t *mod = tilly_alloc(ctx->heap, sizeof(tilly_module_t), 8);
    if (!mod) {
        dlclose(handle);
        pthread_mutex_unlock(&ctx->module_lock);
        return NULL;
    }
    
    // Extract module name from path
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    char *dot = strrchr(name, '.');
    size_t name_len = dot ? (size_t)(dot - name) : strlen(name);
    
    char *mod_name = tilly_alloc(ctx->heap, name_len + 1, 1);
    if (!mod_name) {
        tilly_free(ctx->heap, mod);
        dlclose(handle);
        pthread_mutex_unlock(&ctx->module_lock);
        return NULL;
    }
    memcpy(mod_name, name, name_len);
    mod_name[name_len] = '\0';
    
    mod->name = mod_name;
    mod->version = api->version ? api->version : "1.0";
    mod->handle = handle;
    mod->api = api;
    mod->ref_count = 1;
    
    // Call init
    if (api->init) {
        int result = api->init(ctx);
        if (result != 0) {
            tilly_log_error("module", "Module %s init failed: %d", mod_name, result);
            tilly_free(ctx->heap, mod_name);
            tilly_free(ctx->heap, mod);
            dlclose(handle);
            pthread_mutex_unlock(&ctx->module_lock);
            return NULL;
        }
    }
    
    ctx->modules[ctx->module_count++] = mod;
    tilly_log_info("module", "Loaded module: %s v%s", mod->name, mod->version);
    
    pthread_mutex_unlock(&ctx->module_lock);
    return mod;
}

void tilly_module_unload(struct tilly_context *ctx, tilly_module_t *mod) {
    if (!ctx || !mod) return;
    
    pthread_mutex_lock(&ctx->module_lock);
    
    if (mod->ref_count > 1) {
        mod->ref_count--;
        pthread_mutex_unlock(&ctx->module_lock);
        return;
    }
    
    // Call shutdown
    if (mod->api && mod->api->shutdown) {
        mod->api->shutdown(ctx);
    }
    
    // Close handle
    if (mod->handle) {
        dlclose(mod->handle);
    }
    
    // Free module name
    if (mod->name) {
        tilly_free(ctx->heap, (void *)mod->name);
    }
    
    // Remove from array
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (ctx->modules[i] == mod) {
            ctx->modules[i] = ctx->modules[--ctx->module_count];
            break;
        }
    }
    
    tilly_free(ctx->heap, mod);
    tilly_log_info("module", "Unloaded module");
    
    pthread_mutex_unlock(&ctx->module_lock);
}

tilly_module_t *tilly_module_find(struct tilly_context *ctx, const char *name) {
    if (!ctx || !name) return NULL;
    
    pthread_mutex_lock(&ctx->module_lock);
    
    for (uint32_t i = 0; i < ctx->module_count; i++) {
        if (ctx->modules[i] && strcmp(ctx->modules[i]->name, name) == 0) {
            pthread_mutex_unlock(&ctx->module_lock);
            return ctx->modules[i];
        }
    }
    
    pthread_mutex_unlock(&ctx->module_lock);
    return NULL;
}

void *tilly_module_get_symbol(tilly_module_t *mod, const char *symbol) {
    if (!mod || !mod->handle || !symbol) return NULL;
    return dlsym(mod->handle, symbol);
}

tilly_context_t *tilly_module_get_context(tilly_module_t *mod) {
    (void)mod;
    return g_context;
}

// Set the global context (called from tilly_init)
void tilly_set_global_context(struct tilly_context *ctx) {
    g_context = ctx;
}
