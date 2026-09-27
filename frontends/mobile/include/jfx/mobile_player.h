#ifndef JFX_MOBILE_PLAYER_H
#define JFX_MOBILE_PLAYER_H

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

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
jfx_result_t jfx_mobile_player_render(jfx_mobile_player_t *player,
    double elapsed_seconds);
jfx_result_t jfx_mobile_player_get_state(const jfx_mobile_player_t *player,
    jfx_mobile_player_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* JFX_MOBILE_PLAYER_H */
