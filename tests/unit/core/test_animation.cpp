#include "jfx/jfx_animation.h"
#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
    auto *s = jfx_animation_scene_create(); assert(s);
    jfx_animation_bone_desc_t root{sizeof(root), "root", UINT32_MAX, {{0, 0}, 0, {1, 1}}};
    uint32_t bone = UINT32_MAX; assert(jfx_animation_scene_add_bone(s, &root, &bone) == JFX_SUCCESS);
    jfx_animation_key_t a{sizeof(a), bone, 0, 0.0, 0.0f, JFX_ANIMATION_CURVE_LINEAR};
    jfx_animation_key_t b{sizeof(b), bone, 0, 1.0, 10.0f, JFX_ANIMATION_CURVE_LINEAR};
    assert(jfx_animation_scene_add_key(s, &a) == JFX_SUCCESS);
    assert(jfx_animation_scene_add_key(s, &b) == JFX_SUCCESS);
    jfx_animation_transform_t tr[1] = {{{0, 0}, 0, {1, 1}}};
    jfx_animation_pose_t pose{sizeof(pose), 1, tr};
    assert(jfx_animation_scene_evaluate(s, 0.5, &pose) == JFX_SUCCESS);
    assert(std::fabs(tr[0].translation.x - 5.0f) < 0.001f);
    uint8_t *bytes = nullptr; size_t size = 0;
    assert(jfx_animation_compile(s, &bytes, &size) == JFX_SUCCESS);
    assert(jfx_animation_validate(bytes, size) == JFX_SUCCESS);
    tr[0].translation.x = 0;
    assert(jfx_animation_play(bytes, size, 0.5, &pose) == JFX_SUCCESS);
    assert(std::fabs(tr[0].translation.x - 5.0f) < 0.001f);
    jfx_animation_bytes_destroy(bytes); jfx_animation_scene_destroy(s);
}
