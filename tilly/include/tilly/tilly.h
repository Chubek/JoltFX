#ifndef TILLY_H
#define TILLY_H

#include "tillyz/tillyz.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "tilly/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tilly_runtime tilly_runtime_t;

// Initialize the full Tilly runtime
tilly_runtime_t *tilly_init(const tilly_allocator_t *allocator);

// Shutdown the runtime
void tilly_shutdown(tilly_runtime_t *runtime);

#ifdef __cplusplus
}
#endif

#endif // TILLY_H
