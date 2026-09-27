#include "jfx/mobile_player.h"

#include <assert.h>

int main(void) {
    jfx_mobile_player_t *player = NULL;
    jfx_mobile_player_config_t invalid = { .size = sizeof(invalid), .width = 0,
        .height = 720, .duration_seconds = 10.0 };
    assert(jfx_mobile_player_create(&invalid, &player) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_mobile_player_config_t config = { .size = sizeof(config), .width = 1280,
        .height = 720, .duration_seconds = 10.0, .backend_name = "webgpu" };
    assert(jfx_mobile_player_create(&config, &player) == JFX_SUCCESS);
    assert(jfx_mobile_player_tap(player) == JFX_SUCCESS);
    assert(jfx_mobile_player_render(player, 2.5) == JFX_SUCCESS);
    assert(jfx_mobile_player_swipe(player, 640.0) == JFX_SUCCESS);
    assert(jfx_mobile_player_pinch(player, 2.0) == JFX_SUCCESS);
    jfx_mobile_player_state_t state = { .size = sizeof(state) };
    assert(jfx_mobile_player_get_state(player, &state) == JFX_SUCCESS);
    assert(state.playing == 1 && state.time_seconds > 7.4 && state.time_seconds < 7.6);
    assert(state.viewport_scale == 2.0);
    assert(jfx_mobile_player_pinch(player, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_mobile_player_destroy(player);
    return 0;
}
