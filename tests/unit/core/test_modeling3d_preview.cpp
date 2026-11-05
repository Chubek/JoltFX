#include "modeling3d_preview.h"
#include "tilly/memory.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

int main() {
    auto *scene = jfx_scene3d_create(); assert(scene);
    tilly::vector<uint8_t> pixels(127 * 93 * 4, 99), legacy(pixels.size());
    const float black[3] = {0, 0, 0}, blue[3] = {.125f, .25f, .5f};
    for (bool aa : {false, true}) {
        assert(jfx::detail::scene3d_render_preview(scene, 0, 127, 93, JFX_SCENE3D_SHADING_GOURAUD,
            black, aa, pixels.data(), pixels.size()) == JFX_SUCCESS);
        for (size_t i = 0; i < pixels.size(); i += 4) assert(!pixels[i] && !pixels[i+1] && !pixels[i+2] && pixels[i+3] == 255);
        assert(jfx::detail::scene3d_render_preview(scene, 0, 127, 93, JFX_SCENE3D_SHADING_GOURAUD,
            blue, aa, pixels.data(), pixels.size()) == JFX_SUCCESS);
        assert(pixels[0] == 32 && pixels[1] == 64 && pixels[2] == 128 && pixels[3] == 255);
    }
    assert(jfx_scene3d_command(scene, "3d.add", 16, 0, 0, 0, "sphere") == JFX_SUCCESS);
    for (auto mode : {JFX_SCENE3D_SHADING_PHONG, JFX_SCENE3D_SHADING_GOURAUD, JFX_SCENE3D_SHADING_WIREFRAME}) {
        assert(jfx::detail::scene3d_render_preview(scene, 0, 127, 93, mode, black, false, pixels.data(), pixels.size()) == JFX_SUCCESS);
        assert(pixels[0] == 0 && pixels[1] == 0 && pixels[2] == 0);
        size_t visible = 0;
        for (size_t i = 0; i < pixels.size(); i += 4) { assert(pixels[i+3] == 255); visible += pixels[i] || pixels[i+1] || pixels[i+2]; }
        assert(visible > 10);
    }
    std::fill(pixels.begin(), pixels.end(), 99);
    float bad[3] = {0, NAN, 0};
    assert(jfx::detail::scene3d_render_preview(scene, 0, 127, 93, JFX_SCENE3D_SHADING_PHONG, bad, false,
        pixels.data(), pixels.size()) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx::detail::scene3d_render_preview(scene, 0, 127, 93, JFX_SCENE3D_SHADING_PHONG, black, false,
        pixels.data(), pixels.size()-1) == JFX_ERROR_INVALID_ARGUMENT);
    for (auto byte : pixels) assert(byte == 99);
    // Preferences never change the stable export/render API's default colors.
    assert(jfx_scene3d_render(scene, 0, 127, 93, legacy.data(), legacy.size()) == JFX_SUCCESS);
    assert(legacy[0] == 22 && legacy[1] == 26 && legacy[2] == 34);
    jfx_scene3d_destroy(scene);
}
