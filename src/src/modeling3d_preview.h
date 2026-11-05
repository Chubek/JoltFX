#ifndef JFX_MODELING3D_PREVIEW_H
#define JFX_MODELING3D_PREVIEW_H
#include "jfx/jfx_modeling3d.h"
#include <array>
namespace jfx::detail {
// Private editing profile: caller-selected sRGB clear color and one coverage
// sample while interacting. Public production renders retain their contract.
jfx_result_t scene3d_render_preview(const jfx_scene3d_t *scene, double seconds,
    uint32_t width, uint32_t height, jfx_scene3d_shading_t shading,
    const float background[3], bool antialias, uint8_t *out, size_t capacity);
// Private direct-modeler adapter into the SAME editing raster. Coordinates are
// already camera-relative float values; persistent topology remains elsewhere.
struct EditingTriangle {
    std::array<std::array<float,3>,3> positions;
    std::array<float,3> color;
};
jfx_result_t render_editing_triangles(const EditingTriangle *,size_t count,
    const float quaternion[4],float distance,float orthographic_height,
    uint32_t width,uint32_t height,const float background[3],bool antialias,
    uint8_t *out,size_t capacity);
}
#endif
