#include "tillyz/tillyz.h"
#include <stdio.h>
#include <stdlib.h>

struct tillyz_context {
    int initialized;
};

tillyz_context_t *tillyz_init(void) {
    tillyz_context_t *ctx = malloc(sizeof(tillyz_context_t));
    if (!ctx) {
        tillyz_panic("Failed to allocate bootstrap context");
    }
    ctx->initialized = 1;
    return ctx;
}

void tillyz_shutdown(tillyz_context_t *ctx) {
    if (ctx) {
        free(ctx);
    }
}

void tillyz_panic(const char *message) {
    fprintf(stderr, "PANIC: %s\n", message);
    abort();
}
