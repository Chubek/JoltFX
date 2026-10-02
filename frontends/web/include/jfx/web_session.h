#ifndef JFX_WEB_SESSION_H
#define JFX_WEB_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"
#include "jfx/jfx_editor.h"
#include "jfx/jfx_export.h"

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
jfx_result_t jfx_web_session_export_begin(jfx_web_session_t *session,const char *path,const char *codec,
    uint32_t start_frame,uint32_t frame_count,bool audio,jfx_export_job_t **out_job);
uint32_t jfx_web_export_completed(const jfx_export_job_t *job);
jfx_result_t jfx_web_session_render_audio(jfx_web_session_t *session,double sample,uint32_t rate,
    uint32_t frames,float *out_stereo,size_t capacity);
jfx_result_t jfx_web_session_audio_mixer(jfx_web_session_t *session,uint32_t rate,jfx_audio_mixer_t **out_mixer);
jfx_result_t jfx_web_audio_mixer_render(jfx_audio_mixer_t *mixer,double sample,uint32_t frames,float *out_stereo,size_t capacity);
jfx_result_t jfx_web_session_load_document(jfx_web_session_t *session, const char *text, size_t length,
    char *out_error, size_t error_size);
jfx_result_t jfx_web_session_save_document(jfx_web_session_t *session, char *out_text, size_t capacity, size_t *out_written);
/* Read the sequence for clip/color inspectors while the graph is active. Does
 * not change the active preview/save mode or history. */
jfx_result_t jfx_web_session_save_sequence(jfx_web_session_t *session,char *out_text,size_t capacity,size_t *out_written);

jfx_result_t jfx_web_session_edit(jfx_web_session_t *session, const char *op, uint32_t a,
    uint32_t b, uint32_t c, double value, const char *text);
jfx_result_t jfx_web_session_sequence_state(jfx_web_session_t *session, char *out_json, size_t capacity);
jfx_result_t jfx_web_session_render_frame(jfx_web_session_t *session,uint32_t frame,
    uint32_t width,uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_web_session_graph_state(jfx_web_session_t *session,char *out_json,size_t capacity);
jfx_result_t jfx_web_session_render_graph(jfx_web_session_t *session,uint32_t node,double seconds,
    uint32_t width,uint32_t height,uint8_t *out_rgba,size_t capacity);
#ifdef __cplusplus
}
#endif

#endif /* JFX_WEB_SESSION_H */
