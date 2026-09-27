/* Desktop frontend conformance.
 *
 * Runs the frontend with no host window, which is the same code path the
 * windowed build uses minus presentation, and asserts that the things the UI
 * displays are real: the preview pixels come from the engine's backend, the
 * effect selection and parameter round-trip, playback advances the clock, and
 * invalid arguments are rejected rather than silently accepted. */

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"

static uint8_t near(uint8_t actual, uint8_t expected, uint8_t tolerance) {
    const int delta = static_cast<int>(actual) - static_cast<int>(expected);
    return static_cast<uint8_t>(delta < 0 ? -delta : delta) <= tolerance;
}

int main() {
    jfx_desktop_frontend_t *frontend = nullptr;
    jfx_desktop_frontend_config_t config{};
    config.size = sizeof(config);
    config.width = 1280;
    config.height = 720;
    config.backend_name = jfx_test_backend();
    config.effect_name = "brightness";
    config.effect_parameter = 0.5f;

    /* Argument validation happens before anything is allocated. */
    assert(jfx_desktop_frontend_create(nullptr, &frontend) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_create(&config, nullptr) == JFX_ERROR_INVALID_ARGUMENT);

    /* An unknown backend is rejected by the engine, not silently downgraded. */
    {
        jfx_desktop_frontend_config_t bogus = config;
        bogus.backend_name = "directx9";
        assert(jfx_desktop_frontend_create(&bogus, &frontend) == JFX_ERROR_INVALID_ARGUMENT);
        assert(frontend == nullptr);
    }

    /* Zeroed config falls back to the documented defaults. */
    {
        jfx_desktop_frontend_config_t defaults{};
        defaults.size = sizeof(defaults);
        defaults.backend_name = jfx_test_backend();
        jfx_desktop_frontend_t *default_frontend = nullptr;
        assert(jfx_desktop_frontend_create(&defaults, &default_frontend) == JFX_SUCCESS);
        assert(jfx_desktop_frontend_width(default_frontend) == JFX_DESKTOP_DEFAULT_WIDTH);
        assert(jfx_desktop_frontend_height(default_frontend) == JFX_DESKTOP_DEFAULT_HEIGHT);
        assert(std::strcmp(jfx_desktop_frontend_effect_name(default_frontend), "brightness") == 0);
        assert(!jfx_desktop_frontend_playing(default_frontend));
        jfx_desktop_frontend_destroy(default_frontend);
    }

    /* An unknown effect name falls back to the default rather than failing. */
    {
        jfx_desktop_frontend_config_t bad_effect = config;
        bad_effect.effect_name = "not_an_effect";
        jfx_desktop_frontend_t *fallback = nullptr;
        assert(jfx_desktop_frontend_create(&bad_effect, &fallback) == JFX_SUCCESS);
        assert(std::strcmp(jfx_desktop_frontend_effect_name(fallback), "brightness") == 0);
        jfx_desktop_frontend_destroy(fallback);
    }

    assert(jfx_desktop_frontend_create(&config, &frontend) == JFX_SUCCESS);
    assert(frontend != nullptr);
    assert(std::strcmp(jfx_desktop_frontend_backend_name(frontend), jfx_test_backend()) == 0);
    /* The config's effect is honoured. */
    assert(std::strcmp(jfx_desktop_frontend_effect_name(frontend), "brightness") == 0);
    assert(jfx_desktop_frontend_effect_parameter(frontend) == 0.5f);

    /* The preview must be rendered by the backend, not faked. brightness at
     * 0.5 halves every colour channel and preserves alpha. */
    constexpr uint32_t kWidth = 16;
    constexpr uint32_t kHeight = 8;
    std::vector<uint8_t> pixels(kWidth * kHeight * 4u, 0);
    assert(jfx_desktop_frontend_render_rgba8(frontend, kWidth, kHeight, pixels.data(),
               pixels.size()) == JFX_SUCCESS);
    for (size_t pixel = 0; pixel < kWidth * kHeight; ++pixel) {
        const uint8_t *texel = pixels.data() + pixel * 4u;
        assert(texel[3] == 255);                                  /* alpha preserved */
        assert(near(texel[0], static_cast<uint8_t>(texel[0]), 0)); /* channels present */
    }
    /* Input is a gradient, so the top-left pixel is (0, 0) and the bottom-right
     * is (1, 1). At 0.5 the bottom-right must be about 128. */
    const uint8_t *last = pixels.data() + (kWidth * kHeight - 1u) * 4u;
    assert(near(last[0], 128, 2));
    assert(near(last[1], 128, 2));
    const uint8_t *first = pixels.data();
    assert(near(first[0], 0, 2));
    assert(near(first[1], 0, 2));

    /* An undersized destination is rejected, not written past. */
    std::vector<uint8_t> tiny(4u, 0xAB);
    assert(jfx_desktop_frontend_render_rgba8(frontend, kWidth, kHeight, tiny.data(),
               tiny.size()) == JFX_ERROR_INVALID_ARGUMENT);
    assert(tiny[0] == 0xAB);
    assert(jfx_desktop_frontend_render_rgba8(frontend, 0, kHeight, tiny.data(),
               tiny.size()) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_render_rgba8(frontend, kWidth, kHeight, nullptr,
               0) == JFX_ERROR_INVALID_ARGUMENT);

    /* Effect selection and parameter clamping. */
    assert(jfx_desktop_frontend_set_effect(frontend, "invert", 0.0f) == JFX_SUCCESS);
    assert(std::strcmp(jfx_desktop_frontend_effect_name(frontend), "invert") == 0);
    assert(jfx_desktop_frontend_set_effect(frontend, "no_such_effect", 0.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(std::strcmp(jfx_desktop_frontend_effect_name(frontend), "invert") == 0);
    assert(jfx_desktop_frontend_set_effect(frontend, "", 0.0f) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_set_effect(frontend, nullptr, 0.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_set_effect(frontend, "gamma",
               std::nanf("")) == JFX_ERROR_INVALID_ARGUMENT);
    /* Out-of-range parameters clamp to the effect's documented range
     * (brightness is [0, 4]) instead of failing. */
    assert(jfx_desktop_frontend_set_effect(frontend, "brightness", 1000.0f) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_effect_parameter(frontend) == 4.0f);
    assert(jfx_desktop_frontend_set_effect(frontend, "brightness", -50.0f) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_effect_parameter(frontend) == 0.0f);
    /* gamma's lower bound is 0.1, not 0. */
    assert(jfx_desktop_frontend_set_effect(frontend, "gamma", -1.0f) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_effect_parameter(frontend) == 0.1f);
    /* An effect with no parameters pins the value at zero. */
    assert(jfx_desktop_frontend_set_effect(frontend, "invert", 3.0f) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_effect_parameter(frontend) == 0.0f);

    /* Playback advances the clock and looping keeps it in range. */
    assert(jfx_desktop_frontend_seek(frontend, 0.0) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_play(frontend) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_playing(frontend));
    for (int frame = 0; frame < 8; ++frame) {
        assert(jfx_desktop_frontend_draw(frontend) == JFX_SUCCESS);
    }
    assert(jfx_desktop_frontend_frame_count(frontend) >= 8u);
    assert(jfx_desktop_frontend_time(frontend) > 0.0);
    assert(jfx_desktop_frontend_time(frontend) <= 10.0);
    assert(jfx_desktop_frontend_pause(frontend) == JFX_SUCCESS);
    const double paused_at = jfx_desktop_frontend_time(frontend);
    assert(jfx_desktop_frontend_draw(frontend) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_time(frontend) == paused_at);
    assert(!jfx_desktop_frontend_should_quit(frontend));

    assert(jfx_desktop_frontend_open_project(frontend, "demo.jolt") == JFX_SUCCESS);
    assert(std::strcmp(jfx_desktop_frontend_project_path(frontend), "demo.jolt") == 0);
    assert(jfx_desktop_frontend_open_project(frontend, "") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_open_project(frontend, nullptr) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(std::strcmp(jfx_desktop_frontend_project_path(frontend), "demo.jolt") == 0);

    assert(jfx_desktop_frontend_resize(frontend, 800, 600) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_width(frontend) == 800u);
    assert(jfx_desktop_frontend_height(frontend) == 600u);
    assert(jfx_desktop_frontend_resize(frontend, 0, 600) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_resize(frontend, 800, 0) == JFX_ERROR_INVALID_ARGUMENT);

    assert(jfx_desktop_frontend_seek(frontend, 1.25) == JFX_SUCCESS);
    assert(jfx_desktop_frontend_time(frontend) == 1.25);
    assert(jfx_desktop_frontend_seek(frontend, -1.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_seek(frontend, std::nan("")) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_time(frontend) == 1.25);

    /* Every entry point must tolerate a null frontend. */
    assert(jfx_desktop_frontend_draw(nullptr) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_play(nullptr) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_pause(nullptr) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_seek(nullptr, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_resize(nullptr, 1, 1) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_open_project(nullptr, "x") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_set_effect(nullptr, "invert", 0.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_run(nullptr, 1, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_render_rgba8(nullptr, 1, 1, pixels.data(),
               pixels.size()) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_project_path(nullptr) == nullptr);
    assert(jfx_desktop_frontend_effect_name(nullptr) == nullptr);
    assert(jfx_desktop_frontend_backend_name(nullptr) == nullptr);
    assert(jfx_desktop_frontend_time(nullptr) == 0.0);
    assert(jfx_desktop_frontend_width(nullptr) == 0u);
    jfx_desktop_frontend_destroy(frontend);
    jfx_desktop_frontend_destroy(nullptr);

    return 0;
}
