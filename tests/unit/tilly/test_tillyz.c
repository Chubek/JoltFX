#include "tillyz/tillyz.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    tillyz_context_t *ctx = tillyz_init(NULL);
    assert(ctx != NULL);

    assert(tillyz_arena_alloc(&ctx->arena, 16, 0) == NULL);
    assert(tillyz_arena_alloc(&ctx->arena, 16, 16) != NULL);
    tillyz_shutdown(ctx);
    _Alignas(tillyz_context_t) unsigned char buffer[4096];
    tillyz_config_t cfg = {.arena_size = sizeof(buffer), .arena_buffer = buffer};
    ctx = tillyz_init(&cfg);
    assert(ctx);
    tillyz_shutdown(ctx);
    buffer[0] = 42; // user-owned storage remains valid
    assert(buffer[0] == 42);

    printf("TillyZ tests passed\n");
    return 0;
}
