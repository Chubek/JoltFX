#ifndef TILLY_RUNTIME_INTERNAL_H
#define TILLY_RUNTIME_INTERNAL_H
#include "tilly/tilly.h"
#include <pthread.h>
#define TILLY_MAX_MODULES 32
typedef struct {
    tilly_module_t module;
    tilly_context_t *owner;
    char *path;
    bool initializing;
} tilly_module_entry_t;
struct tilly_context {
    tillyz_context_t *bootstrap_ctx;
    tilly_allocator_t *heap;
    tilly_module_entry_t *modules[TILLY_MAX_MODULES];
    uint32_t module_count;
    pthread_mutex_t module_lock;
    /* Lifecycle changes are serialized; registry readers can run in callbacks. */
    pthread_mutex_t lifecycle_lock;
    bool shutting_down;
};
#endif
