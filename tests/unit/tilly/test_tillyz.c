#include "tillyz/tillyz.h"
#include <stdio.h>
#include <assert.h>

int main(void) {
    tillyz_context_t *ctx = tillyz_init(NULL);
    assert(ctx != NULL);
    assert(ctx->owns_arena);
    assert(ctx->platform == tillyz_detect_platform());
#ifdef __EMSCRIPTEN__
    assert(ctx->platform == TILLYZ_PLATFORM_WASM);
    assert(tillyz_time_now_ns() > 0);
#endif

    assert(tillyz_arena_alloc(&ctx->arena, 16, 0) == NULL);
    assert(tillyz_arena_alloc(&ctx->arena, 16, 16) != NULL);
    tillyz_shutdown(ctx);
    _Alignas(tillyz_context_t) unsigned char buffer[4096];
    tillyz_config_t cfg = {.arena_size = sizeof(buffer), .arena_buffer = buffer};
    ctx = tillyz_init(&cfg);
    assert(ctx);
    assert(!ctx->owns_arena);
    tillyz_shutdown(ctx);
    buffer[0] = 42; // user-owned storage remains valid
    assert(buffer[0] == 42);

    cfg.arena_size = sizeof(tillyz_context_t) - 1;
    assert(tillyz_init(&cfg) == NULL);
    cfg.arena_buffer = NULL;
    assert(tillyz_init(&cfg) == NULL);
    cfg.arena_size = sizeof(buffer) - 1;
    cfg.arena_buffer = buffer + 1;
    assert(tillyz_init(&cfg) == NULL);

    // Exercise owned-map release separately from caller-buffer shutdown.
    for (int i = 0; i < 8; ++i) {
        ctx = tillyz_init(NULL);
        assert(ctx);
        assert(tillyz_arena_alloc(&ctx->arena, 128, 16));
        tillyz_shutdown(ctx);
    }

    printf("TillyZ tests passed\n");
    return 0;
}
