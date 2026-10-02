#ifndef TILLY_MODULE_H
#define TILLY_MODULE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_module tilly_module_t;
typedef struct tilly_context tilly_context_t;

typedef struct {
    const char *name;
    const char *version;
    int (*init)(tilly_context_t *ctx);
    void (*shutdown)(tilly_context_t *ctx);
} tilly_module_api_t;

// Module handle (opaque)
struct tilly_module {
    const char *name;
    const char *version;
    void *handle;
    tilly_module_api_t *api;
    uint32_t ref_count;
};

// Load a module (dynamic or static)
tilly_module_t *tilly_module_load(tilly_context_t *ctx, const char *path);

// Unload a module
void tilly_module_unload(tilly_context_t *ctx, tilly_module_t *mod);

// Find a loaded module by name
tilly_module_t *tilly_module_find(tilly_context_t *ctx, const char *name);

// Get a symbol from a module
void *tilly_module_get_symbol(tilly_module_t *mod, const char *symbol);

// Module registration macro for static modules
#define TILLY_MODULE_EXPORT __attribute__((visibility("default")))

#ifdef __cplusplus
}
#endif

#endif // TILLY_MODULE_H
