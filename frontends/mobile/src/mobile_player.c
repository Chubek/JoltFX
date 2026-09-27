/* Mobile playback surface.
 *
 * Owns a Core engine, the playback clock and the touch gesture mapping, and
 * renders real frames through the engine's backend for the platform surface
 * (Android SurfaceView, iOS CAMetalDrawable) to display. */

#include "jfx/mobile_player.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "joltscript/effects.h"
#include "tilly/allocator.h"

#define MOBILE_EFFECT_NAME_MAX 32
#define MOBILE_MAX_DIMENSION 8192u

struct jfx_mobile_player {
    jfx_engine_t *engine;
    jolt_effects_t *effects;
    char effect_name[MOBILE_EFFECT_NAME_MAX];
    float effect_parameter;
    int has_parameter;
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

static uint8_t to_unorm8(float value) {
    if (!(value > 0.0f)) return 0u; /* also catches NaN */
    if (value >= 1.0f) return 255u;
    return (uint8_t)lrintf(value * 255.0f);
}

jfx_result_t jfx_mobile_player_create(const jfx_mobile_player_config_t *config,
    jfx_mobile_player_t **out_player) {
    if (!config || !out_player || config->size < sizeof(*config) || !config->width ||
        !config->height || !isfinite(config->duration_seconds) || config->duration_seconds <= 0.0 ||
        config->width > MOBILE_MAX_DIMENSION || config->height > MOBILE_MAX_DIMENSION) {
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
    player->effects = jolt_effects_create();
    if (!player->effects) {
        jfx_engine_shutdown(player->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), player);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    player->width = config->width;
    player->height = config->height;
    player->duration_seconds = config->duration_seconds;
    player->viewport_scale = 1.0;
    snprintf(player->effect_name, sizeof(player->effect_name), "%s", "brightness");
    player->has_parameter =
        jolt_effects_parameter_count(player->effects, player->effect_name) > 0;
    *out_player = player;
    return JFX_SUCCESS;
}

void jfx_mobile_player_destroy(jfx_mobile_player_t *player) {
    if (!player) return;
    jolt_effects_destroy(player->effects);
    jfx_engine_shutdown(player->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), player);
}

jfx_result_t jfx_mobile_player_set_viewport(jfx_mobile_player_t *player,
    uint32_t width, uint32_t height) {
    if (!player || !width || !height || width > MOBILE_MAX_DIMENSION ||
        height > MOBILE_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
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

/* Advances the playback clock by `elapsed_seconds`, looping at the end. */
static void advance(jfx_mobile_player_t *player, double elapsed_seconds) {
    if (!player->playing) {
        return;
    }
    player->time_seconds += elapsed_seconds;
    while (player->time_seconds >= player->duration_seconds) {
        player->time_seconds -= player->duration_seconds;
    }
}

jfx_result_t jfx_mobile_player_render(jfx_mobile_player_t *player,
    double elapsed_seconds) {
    if (!player || !isfinite(elapsed_seconds) || elapsed_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    advance(player, elapsed_seconds);
    return jfx_engine_tick(player->engine);
}

jfx_result_t jfx_mobile_player_set_effect(jfx_mobile_player_t *player,
    const char *effect_name, float parameter) {
    if (!player || !effect_name || !effect_name[0] || !isfinite(parameter) ||
        strlen(effect_name) >= sizeof(player->effect_name) ||
        !jolt_effects_exists(player->effects, effect_name)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const int has_parameter =
        jolt_effects_parameter_count(player->effects, effect_name) > 0;
    if (has_parameter) {
        float lo = 0.0f;
        float hi = 0.0f;
        jolt_effects_parameter_range(player->effects, effect_name, &lo, &hi);
        parameter = parameter < lo ? lo : (parameter > hi ? hi : parameter);
    } else {
        parameter = 0.0f;
    }
    snprintf(player->effect_name, sizeof(player->effect_name), "%s", effect_name);
    player->effect_parameter = parameter;
    player->has_parameter = has_parameter;
    return JFX_SUCCESS;
}

const char *jfx_mobile_player_effect_name(const jfx_mobile_player_t *player) {
    return player ? player->effect_name : NULL;
}

jfx_result_t jfx_mobile_player_render_rgba8(jfx_mobile_player_t *player,
    double elapsed_seconds, uint8_t *out_rgba, size_t out_size) {
    if (!player || !isfinite(elapsed_seconds) || elapsed_seconds < 0.0 || !out_rgba) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* The pinch scale multiplies the rendered resolution around the viewport. */
    const double scale = player->viewport_scale;
    if (!isfinite(scale) || scale <= 0.0) return JFX_ERROR_INVALID_ARGUMENT;
    const size_t width = (size_t)((double)player->width * scale + 0.5);
    const size_t height = (size_t)((double)player->height * scale + 0.5);
    if (!width || !height || width > MOBILE_MAX_DIMENSION || height > MOBILE_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (width > SIZE_MAX / height / 4u) return JFX_ERROR_OUT_OF_MEMORY;
    const size_t pixels = width * height;
    if (out_size < pixels * 4u) return JFX_ERROR_INVALID_ARGUMENT;

    size_t bytecode_size = 0;
    const uint8_t *bytecode =
        jolt_effects_bytecode(player->effects, player->effect_name, &bytecode_size);
    if (!bytecode || !bytecode_size) return JFX_ERROR_NOT_FOUND;

    const size_t float_bytes = pixels * 4u * sizeof(float);
    float *input = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), float_bytes,
        _Alignof(float));
    float *output = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), float_bytes,
        _Alignof(float));
    if (!input || !output) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), input);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), output);
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    const double phase = player->time_seconds - floor(player->time_seconds);
    for (size_t y = 0; y < height; ++y) {
        for (size_t x = 0; x < width; ++x) {
            const size_t index = (y * width + x) * 4u;
            input[index] = (float)x / (float)(width > 1u ? width - 1u : 1u);
            input[index + 1u] = (float)y / (float)(height > 1u ? height - 1u : 1u);
            input[index + 2u] = (float)phase;
            input[index + 3u] = 1.0f;
        }
    }

    const float *parameters = player->has_parameter ? &player->effect_parameter : NULL;
    const size_t parameter_count = player->has_parameter ? 1u : 0u;
    jfx_result_t status = jfx_engine_execute_bytecode(player->engine, bytecode, bytecode_size,
        input, pixels, parameters, parameter_count, output);
    if (status == JFX_SUCCESS) {
        for (size_t i = 0; i < pixels * 4u; ++i) {
            out_rgba[i] = to_unorm8(output[i]);
        }
        advance(player, elapsed_seconds);
        status = jfx_engine_tick(player->engine);
    }

    tilly_free((tilly_allocator_t *)tilly_default_allocator(), input);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), output);
    return status;
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
