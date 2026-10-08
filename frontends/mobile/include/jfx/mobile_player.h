#ifndef JFX_MOBILE_PLAYER_H
#define JFX_MOBILE_PLAYER_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"
#include "jfx/jfx_editor.h"
#include "jfx/jfx_export.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_mobile_player jfx_mobile_player_t;

typedef struct {
    size_t size;
    uint32_t width;
    uint32_t height;
    double duration_seconds;
    const char *backend_name;
} jfx_mobile_player_config_t;

typedef struct {
    size_t size;
    uint32_t width;
    uint32_t height;
    double time_seconds;
    double viewport_scale;
    uint32_t playing;
} jfx_mobile_player_state_t;

jfx_result_t jfx_mobile_player_create(const jfx_mobile_player_config_t *config,
    jfx_mobile_player_t **out_player);
void jfx_mobile_player_destroy(jfx_mobile_player_t *player);
jfx_result_t jfx_mobile_player_set_viewport(jfx_mobile_player_t *player,
    uint32_t width, uint32_t height);
jfx_result_t jfx_mobile_player_tap(jfx_mobile_player_t *player);
jfx_result_t jfx_mobile_player_swipe(jfx_mobile_player_t *player,
    double horizontal_pixels);
jfx_result_t jfx_mobile_player_pinch(jfx_mobile_player_t *player,
    double scale_delta);
jfx_result_t jfx_mobile_player_set_zoom(jfx_mobile_player_t *player, double zoom);
jfx_result_t jfx_mobile_player_render(jfx_mobile_player_t *player,
    double elapsed_seconds);
jfx_result_t jfx_mobile_player_get_state(const jfx_mobile_player_t *player,
    jfx_mobile_player_state_t *out_state);

/* Selects the bundled effect the player renders. Unknown names are rejected;
 * `parameter` is clamped to the effect's documented range. */
jfx_result_t jfx_mobile_player_set_effect(jfx_mobile_player_t *player,
    const char *effect_name, float parameter);
const char *jfx_mobile_player_effect_name(const jfx_mobile_player_t *player);

/* Renders the current frame through the engine's backend into tightly packed
 * RGBA8 at the player's current viewport size, advancing the clock by
 * `elapsed_seconds` when playing. This is what the Android SurfaceView and the
 * iOS drawable bind to; it is a real render, not a clock tick.
 *
 * out_rgba must hold width * height * 4 bytes, adjusted by the viewport scale.
 * Output is untouched on error. */
jfx_result_t jfx_mobile_player_render_rgba8(jfx_mobile_player_t *player,
    double elapsed_seconds, uint8_t *out_rgba, size_t out_size);


/* The borrowed session is shared by NLE, layer effects, grading and nodes. */
jfx_editor_t *jfx_mobile_player_editor(jfx_mobile_player_t *player);
jfx_result_t jfx_mobile_player_scene3d_state(jfx_mobile_player_t *player,char *out_json,size_t capacity);
jfx_result_t jfx_mobile_player_export_begin(jfx_mobile_player_t *player,const jfx_export_options_t *options,jfx_export_job_t **out_job);
jfx_result_t jfx_mobile_player_audio_mixer(jfx_mobile_player_t *player,uint32_t rate,jfx_audio_mixer_t **out_mixer);
jfx_result_t jfx_mobile_player_load_document(jfx_mobile_player_t *player, const char *text, size_t length,
    char *out_error, size_t error_size);
jfx_result_t jfx_mobile_player_save_document(jfx_mobile_player_t *player, char *out_text, size_t capacity, size_t *out_written);

jfx_result_t jfx_mobile_player_edit(jfx_mobile_player_t *player, const char *op, uint32_t a,
    uint32_t b, uint32_t c, double value, const char *text);
jfx_result_t jfx_mobile_player_seek(jfx_mobile_player_t *player, double seconds);
jfx_result_t jfx_mobile_player_sequence_state(jfx_mobile_player_t *player, char *out_json, size_t capacity);
jfx_result_t jfx_mobile_player_write_frame(jfx_mobile_player_t *player,uint64_t frame,const char *path);
jfx_result_t jfx_mobile_player_graph_state(jfx_mobile_player_t *player,char *out_json,size_t capacity);
jfx_result_t jfx_mobile_player_render_graph(jfx_mobile_player_t *player,uint32_t node,double seconds,
    uint32_t width,uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_mobile_player_write_graph(jfx_mobile_player_t *player,uint32_t node,double seconds,const char *path);
#ifdef __cplusplus
}
#endif

#endif /* JFX_MOBILE_PLAYER_H */
