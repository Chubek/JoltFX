#ifndef TILLYZ_H
#define TILLYZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bootstrap API - zero dependency runtime foundation
typedef struct tillyz_context tillyz_context_t;

// Initialize the bootstrap runtime
tillyz_context_t *tillyz_init(void);

// Shutdown and cleanup
void tillyz_shutdown(tillyz_context_t *ctx);

// Panic handler
void tillyz_panic(const char *message);

#ifdef __cplusplus
}
#endif

#endif // TILLYZ_H
