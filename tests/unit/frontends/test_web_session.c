#include "jfx/web_session.h"

#include <assert.h>

int main(void) {
    jfx_web_session_t *session = NULL;
    assert(jfx_web_session_create("webgpu", &session) == JFX_SUCCESS);
    unsigned char pixels[16];
    assert(jfx_web_session_set_effect(session, "brightness", 0.1f) == JFX_SUCCESS);
    assert(jfx_web_session_render_rgba(session, 0.25, 2, 2, pixels, sizeof(pixels)) == JFX_SUCCESS);
    assert(pixels[0] == 0 && pixels[3] == 255 && pixels[4] == 26);
    assert(jfx_web_session_set_effect(session, "nope", 0.0f) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_web_session_render_rgba(session, -1.0, 2, 2, pixels, sizeof(pixels)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_web_session_destroy(session);
    return 0;
}
