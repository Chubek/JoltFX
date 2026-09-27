/* Mobile player conformance: gesture mapping, playback clock, and a real
 * render into RGBA8 that the platform surface can display. */

#include "jfx/mobile_player.h"
#include "jfx_test_backend.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

static uint8_t near(uint8_t actual, uint8_t expected, uint8_t tolerance) {
    const int delta = (int)actual - (int)expected;
    return (uint8_t)(delta < 0 ? -delta : delta) <= tolerance;
}

int main(void) {
    jfx_mobile_player_t *player = NULL;
    jfx_mobile_player_config_t invalid = { .size = sizeof(invalid), .width = 0,
        .height = 720, .duration_seconds = 10.0 };
    assert(jfx_mobile_player_create(&invalid, &player) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_mobile_player_config_t no_duration = { .size = sizeof(no_duration), .width = 640,
        .height = 480, .duration_seconds = 0.0 };
    assert(jfx_mobile_player_create(&no_duration, &player) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_mobile_player_config_t stale_size = { .size = 0, .width = 640, .height = 480,
        .duration_seconds = 10.0 };
    assert(jfx_mobile_player_create(&stale_size, &player) == JFX_ERROR_INVALID_ARGUMENT);

    jfx_mobile_player_config_t config = { .size = sizeof(config), .width = 1280,
        .height = 720, .duration_seconds = 10.0, .backend_name = jfx_test_backend() };
    assert(jfx_mobile_player_create(&config, &player) == JFX_SUCCESS);
    assert(strcmp(jfx_mobile_player_effect_name(player), "brightness") == 0);

    /* A real render, not a clock tick: brightness at 0.5 halves the channels
     * and keeps alpha. The render resolution follows the viewport, so shrink it
     * first to keep the buffer small. */
    assert(jfx_mobile_player_set_effect(player, "brightness", 0.5f) == JFX_SUCCESS);
    assert(jfx_mobile_player_set_effect(player, "not_an_effect", 0.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(strcmp(jfx_mobile_player_effect_name(player), "brightness") == 0);
    assert(jfx_mobile_player_set_effect(player, "brightness", 1000.0f) == JFX_SUCCESS);
    assert(jfx_mobile_player_set_effect(player, "brightness", 0.5f) == JFX_SUCCESS);

    enum { kWidth = 32, kHeight = 16 };
    assert(jfx_mobile_player_set_viewport(player, kWidth, kHeight) == JFX_SUCCESS);
    uint8_t pixels[kWidth * kHeight * 4];
    memset(pixels, 0, sizeof(pixels));
    assert(jfx_mobile_player_render_rgba8(player, 0.0, pixels, sizeof(pixels)) == JFX_SUCCESS);
    for (size_t pixel = 0; pixel < (size_t)kWidth * kHeight; ++pixel) {
        assert(pixels[pixel * 4 + 3] == 255); /* alpha preserved */
    }
    /* Bottom-right of the gradient is (1, 1); at 0.5 it lands near 128. */
    const uint8_t *last = pixels + ((size_t)kWidth * kHeight - 1u) * 4u;
    assert(near(last[0], 128, 2));
    assert(near(last[1], 128, 2));
    const uint8_t *first = pixels;
    assert(near(first[0], 0, 2));

    /* An undersized destination is rejected without writing. */
    uint8_t guard[4];
    memset(guard, 0xAB, sizeof(guard));
    assert(jfx_mobile_player_render_rgba8(player, 0.0, guard, sizeof(guard)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(guard[0] == 0xAB);
    assert(jfx_mobile_player_render_rgba8(player, 0.0, NULL, 0) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_render_rgba8(player, -1.0, pixels, sizeof(pixels)) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* Pinching changes the rendered resolution, so the buffer requirement
     * grows with it. */
    assert(jfx_mobile_player_pinch(player, 2.0) == JFX_SUCCESS);
    jfx_mobile_player_state_t state = { .size = sizeof(state) };
    assert(jfx_mobile_player_get_state(player, &state) == JFX_SUCCESS);
    assert(state.viewport_scale == 2.0);
    assert(jfx_mobile_player_render_rgba8(player, 0.0, pixels, sizeof(pixels)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_pinch(player, 0.5) == JFX_SUCCESS);
    assert(jfx_mobile_player_render_rgba8(player, 0.0, pixels, sizeof(pixels)) == JFX_SUCCESS);
    assert(jfx_mobile_player_pinch(player, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_pinch(player, -1.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_pinch(player, NAN) == JFX_ERROR_INVALID_ARGUMENT);

    /* Gestures and the playback clock. The swipe maps pixels to time through
     * the viewport width, so restore the configured 1280-wide viewport first:
     * a 640 px swipe is exactly half of the 10 s duration. */
    assert(jfx_mobile_player_set_viewport(player, 1280, 720) == JFX_SUCCESS);
    assert(jfx_mobile_player_tap(player) == JFX_SUCCESS);
    assert(jfx_mobile_player_render(player, 2.5) == JFX_SUCCESS);
    assert(jfx_mobile_player_swipe(player, 640.0) == JFX_SUCCESS);
    assert(jfx_mobile_player_get_state(player, &state) == JFX_SUCCESS);
    assert(state.playing == 1 && state.time_seconds > 7.4 && state.time_seconds < 7.6);

    /* The clock loops instead of running off the end. */
    assert(jfx_mobile_player_render(player, 100.0) == JFX_SUCCESS);
    assert(jfx_mobile_player_get_state(player, &state) == JFX_SUCCESS);
    assert(state.time_seconds >= 0.0 && state.time_seconds < 10.0);
    assert(jfx_mobile_player_render(player, NAN) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_render(player, -1.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_swipe(player, INFINITY) == JFX_ERROR_INVALID_ARGUMENT);

    assert(jfx_mobile_player_set_viewport(player, 0, 480) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_set_viewport(player, 640, 480) == JFX_SUCCESS);

    /* Paused, the clock does not move. */
    assert(jfx_mobile_player_tap(player) == JFX_SUCCESS);
    assert(jfx_mobile_player_get_state(player, &state) == JFX_SUCCESS);
    const double paused_at = state.time_seconds;
    assert(jfx_mobile_player_render(player, 1.0) == JFX_SUCCESS);
    assert(jfx_mobile_player_get_state(player, &state) == JFX_SUCCESS);
    assert(state.time_seconds == paused_at);
    assert(state.playing == 0);

    /* Every entry point tolerates a null player. */
    assert(jfx_mobile_player_tap(NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_render(NULL, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_swipe(NULL, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_pinch(NULL, 1.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_set_viewport(NULL, 1, 1) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_set_effect(NULL, "invert", 0.0f) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_render_rgba8(NULL, 0.0, pixels, sizeof(pixels)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_get_state(NULL, &state) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_effect_name(NULL) == NULL);
    state.size = 0;
    assert(jfx_mobile_player_get_state(player, &state) == JFX_ERROR_INVALID_ARGUMENT);

    jfx_mobile_player_destroy(player);
    jfx_mobile_player_destroy(NULL);
    return 0;
}
