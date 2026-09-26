#ifndef TILLY_H
#define TILLY_H

#include <stdbool.h>
#include "tillyz/tillyz.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "tilly/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_context tilly_context_t;

typedef struct {
    tillyz_context_t *bootstrap_ctx;
    size_t heap_size;
    bool enable_logging;
    tilly_log_level_t log_level;
    bool enable_profiling;
} tilly_config_t;

// Initialize the full Tilly runtime
tilly_context_t *tilly_init(const tilly_config_t *config);

// Shutdown the runtime
void tilly_shutdown(tilly_context_t *ctx);

// Get the default heap allocator
tilly_allocator_t *tilly_get_heap_allocator(tilly_context_t *ctx);

// Get the context from a module (for module init)
tilly_context_t *tilly_module_get_context(tilly_module_t *mod);

#ifdef __cplusplus
}
#endif

#endif // TILLY_H