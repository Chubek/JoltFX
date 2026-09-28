#ifndef JFX_WEB_SESSION_H
#define JFX_WEB_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"
#include "jfx/jfx_editor.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_web_session jfx_web_session_t;

jfx_result_t jfx_web_session_create(const char *backend_name,
    jfx_web_session_t **out_session);
void jfx_web_session_destroy(jfx_web_session_t *session);
jfx_result_t jfx_web_session_render(jfx_web_session_t *session, double time_seconds);

/* Select one bundled effect for the browser bridge. A parameter is optional;
 * effects without parameters ignore it. This is intentionally separate from
 * package decoding, which belongs to the browser-side package loader. */
jfx_result_t jfx_web_session_set_effect(jfx_web_session_t *session,
    const char *effect_name, float parameter);

/* Renders a deterministic RGBA8 preview into caller-owned memory. */
jfx_result_t jfx_web_session_render_rgba(jfx_web_session_t *session,
    double time_seconds, uint32_t width, uint32_t height, uint8_t *out_rgba,
    size_t out_size);


/* The borrowed session is shared by NLE, layer effects, grading and nodes. */
jfx_editor_t *jfx_web_session_editor(jfx_web_session_t *session);
jfx_result_t jfx_web_session_load_document(jfx_web_session_t *session, const char *text, size_t length,
    char *out_error, size_t error_size);
jfx_result_t jfx_web_session_save_document(jfx_web_session_t *session, char *out_text, size_t capacity, size_t *out_written);

jfx_result_t jfx_web_session_edit(jfx_web_session_t *session, const char *op, uint32_t a,
    uint32_t b, uint32_t c, double value, const char *text);
#ifdef __cplusplus
}
#endif

#endif /* JFX_WEB_SESSION_H */
