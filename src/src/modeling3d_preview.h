#ifndef JFX_MODELING3D_PREVIEW_H
#define JFX_MODELING3D_PREVIEW_H
#include "jfx/jfx_modeling3d.h"
namespace jfx::detail {
// Private editing profile: caller-selected sRGB clear color and one coverage
// sample while interacting. Public production renders retain their contract.
jfx_result_t scene3d_render_preview(const jfx_scene3d_t *scene, double seconds,
    uint32_t width, uint32_t height, jfx_scene3d_shading_t shading,
    const float background[3], bool antialias, uint8_t *out, size_t capacity);
}
#endif
