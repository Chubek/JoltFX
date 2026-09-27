#include "jfx/mobile_player.h"

#include <math.h>
#include <string.h>

#include "tilly/allocator.h"

struct jfx_mobile_player {
    jfx_engine_t *engine;
    uint32_t width;
    uint32_t height;
    double duration_seconds;
    double time_seconds;
    double viewport_scale;
    int playing;
};

static double clamp(double value, double minimum, double maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

jfx_result_t jfx_mobile_player_create(const jfx_mobile_player_config_t *config,
    jfx_mobile_player_t **out_player) {
    if (!config || !out_player || config->size < sizeof(*config) || !config->width ||
        !config->height || !isfinite(config->duration_seconds) || config->duration_seconds <= 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_player = NULL;
    jfx_mobile_player_t *player = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*player), _Alignof(jfx_mobile_player_t));
    if (!player) return JFX_ERROR_OUT_OF_MEMORY;
    memset(player, 0, sizeof(*player));
    jfx_engine_config_t engine_config = {
        .max_buffers = 2,
        .max_textures = 16,
        .max_kernels = 32,
        .backend_name = config->backend_name,
    };
    jfx_result_t result = jfx_engine_init(&engine_config, &player->engine);
    if (result != JFX_SUCCESS) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), player);
        return result;
    }
    player->width = config->width;
    player->height = config->height;
    player->duration_seconds = config->duration_seconds;
    player->viewport_scale = 1.0;
    *out_player = player;
    return JFX_SUCCESS;
}

void jfx_mobile_player_destroy(jfx_mobile_player_t *player) {
    if (!player) return;
    jfx_engine_shutdown(player->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), player);
}

jfx_result_t jfx_mobile_player_set_viewport(jfx_mobile_player_t *player,
    uint32_t width, uint32_t height) {
    if (!player || !width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    player->width = width;
    player->height = height;
    return JFX_SUCCESS;
}

jfx_result_t jfx_mobile_player_tap(jfx_mobile_player_t *player) {
    if (!player) return JFX_ERROR_INVALID_ARGUMENT;
    player->playing = !player->playing;
    return JFX_SUCCESS;
}

jfx_result_t jfx_mobile_player_swipe(jfx_mobile_player_t *player,
    double horizontal_pixels) {
    if (!player || !isfinite(horizontal_pixels)) return JFX_ERROR_INVALID_ARGUMENT;
    player->time_seconds = clamp(player->time_seconds + horizontal_pixels / (double)player->width *
        player->duration_seconds, 0.0, player->duration_seconds);
    return JFX_SUCCESS;
}

jfx_result_t jfx_mobile_player_pinch(jfx_mobile_player_t *player,
    double scale_delta) {
    if (!player || !isfinite(scale_delta) || scale_delta <= 0.0) return JFX_ERROR_INVALID_ARGUMENT;
    player->viewport_scale = clamp(player->viewport_scale * scale_delta, 0.25, 8.0);
    return JFX_SUCCESS;
}

jfx_result_t jfx_mobile_player_render(jfx_mobile_player_t *player,
    double elapsed_seconds) {
    if (!player || !isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (player->playing) {
        player->time_seconds += elapsed_seconds;
        while (player->time_seconds >= player->duration_seconds) {
            player->time_seconds -= player->duration_seconds;
        }
    }
    return jfx_engine_tick(player->engine);
}

jfx_result_t jfx_mobile_player_get_state(const jfx_mobile_player_t *player,
    jfx_mobile_player_state_t *out_state) {
    if (!player || !out_state || out_state->size < sizeof(*out_state)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    out_state->size = sizeof(*out_state);
    out_state->width = player->width;
    out_state->height = player->height;
    out_state->time_seconds = player->time_seconds;
    out_state->viewport_scale = player->viewport_scale;
    out_state->playing = (uint32_t)player->playing;
    return JFX_SUCCESS;
}
