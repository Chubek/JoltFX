#include "jfx/web_session.h"
#include "jfx_test_backend.h"

#include <assert.h>
#include "tilly/allocator.h"

int main(void) {
    const tilly_allocator_t *allocator = tilly_default_allocator();
    size_t baseline = tilly_allocator_usage(allocator);
    assert(!jfx_web_alloc(0) && !jfx_web_alloc(SIZE_MAX));
    unsigned char *transfer = jfx_web_alloc(32);
    assert(transfer && tilly_allocator_usage(allocator) == baseline + 32);
    jfx_web_session_t *session = NULL;
    assert(jfx_web_session_create(jfx_test_backend(), &session) == JFX_SUCCESS);
    unsigned char pixels[16];
    assert(jfx_web_session_set_effect(session, "brightness", 0.1f) == JFX_SUCCESS);
    assert(jfx_web_session_render_rgba(session, 0.25, 2, 2, pixels, sizeof(pixels)) == JFX_SUCCESS);
    assert(pixels[0] == 0 && pixels[3] == 255 && pixels[4] == 26);
    assert(jfx_web_session_set_effect(session, "nope", 0.0f) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_web_session_render_rgba(session, -1.0, 2, 2, pixels, sizeof(pixels)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_web_session_destroy(session);
    transfer[0] = 42; /* Process-owned buffers outlive sessions. */
    assert(transfer[0] == 42);
    jfx_web_free(transfer);
    jfx_web_free(transfer);
    jfx_web_free(NULL);
    assert(tilly_allocator_usage(allocator) == baseline);
    return 0;
}
