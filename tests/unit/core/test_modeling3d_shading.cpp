#include "jfx/jfx_modeling3d.h"
#include "tilly/memory.hpp"
#include <algorithm>
#include <cassert>
#include <limits>

static size_t coverage(const tilly::vector<uint8_t> &pixels) {
    size_t count=0;
    for (size_t i=0;i<pixels.size();i+=4) {
        assert(pixels[i+3]==255);
        count+=pixels[i]!=22 || pixels[i+1]!=26 || pixels[i+2]!=34;
    }
    return count;
}
int main() {
    auto *scene=jfx_scene3d_create(); assert(scene);
    assert(jfx_scene3d_command(scene,"3d.add",8,0,0,0,"sphere")==JFX_SUCCESS);
    constexpr uint32_t w=160,h=120;
    tilly::vector<uint8_t> phong(w*h*4),gouraud(phong.size()),wire(phong.size()),other(phong.size());
    assert(jfx_scene3d_render(scene,0,w,h,phong.data(),phong.size())==JFX_SUCCESS);
    assert(jfx_scene3d_render_shaded(scene,0,w,h,JFX_SCENE3D_SHADING_PHONG,other.data(),other.size())==JFX_SUCCESS && phong==other);
    assert(jfx_scene3d_render_shaded(scene,0,w,h,JFX_SCENE3D_SHADING_GOURAUD,gouraud.data(),gouraud.size())==JFX_SUCCESS);
    assert(jfx_scene3d_render_shaded(scene,0,w,h,JFX_SCENE3D_SHADING_WIREFRAME,wire.data(),wire.size())==JFX_SUCCESS);
    assert(coverage(gouraud)>w*h/100 && coverage(wire)>0);
    assert(coverage(wire)<coverage(gouraud) && phong!=gouraud && wire!=gouraud);
    for (auto shading:{JFX_SCENE3D_SHADING_PHONG,JFX_SCENE3D_SHADING_GOURAUD,JFX_SCENE3D_SHADING_WIREFRAME}) {
        std::fill(other.begin(),other.end(),99);
        assert(jfx_scene3d_render_shaded(nullptr,0,w,h,shading,other.data(),other.size())==JFX_ERROR_INVALID_ARGUMENT);
        assert(jfx_scene3d_render_shaded(scene,0,w,h,shading,nullptr,other.size())==JFX_ERROR_INVALID_ARGUMENT);
        assert(jfx_scene3d_render_shaded(scene,0,w,h,shading,other.data(),other.size()-1)==JFX_ERROR_INVALID_ARGUMENT);
        assert(jfx_scene3d_render_shaded(scene,0,2049,h,shading,other.data(),other.size())==JFX_ERROR_INVALID_ARGUMENT);
        assert(jfx_scene3d_render_shaded(scene,std::numeric_limits<double>::quiet_NaN(),w,h,shading,other.data(),other.size())==JFX_ERROR_INVALID_ARGUMENT);
        assert(jfx_scene3d_render_shaded(scene,0,w,h,jfx_scene3d_shading_t(3),other.data(),other.size())==JFX_ERROR_INVALID_ARGUMENT);
        for (auto byte:other) assert(byte==99);
        // Through/behind the near plane: clipping and line stepping stay bounded.
        assert(jfx_scene3d_command(scene,"3d.camera",2,0,0,.5,"")==JFX_SUCCESS);
        assert(jfx_scene3d_render_shaded(scene,0,w,h,shading,other.data(),other.size())==JFX_SUCCESS);
        coverage(other);
        assert(jfx_scene3d_command(scene,"3d.camera",2,0,0,6,"")==JFX_SUCCESS);
    }
    assert(jfx_scene3d_command(scene,"3d.visible",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_render_shaded(scene,0,w,h,JFX_SCENE3D_SHADING_WIREFRAME,other.data(),other.size())==JFX_SUCCESS && coverage(other)==0);
    assert(jfx_scene3d_command(scene,"3d.visible",0,0,0,1,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(scene,"3d.script",0,1,0,0,"(defkernel bad [time frame index value] (/ 1 time))")==JFX_SUCCESS);
    for (auto shading:{JFX_SCENE3D_SHADING_GOURAUD,JFX_SCENE3D_SHADING_WIREFRAME}) {
        std::fill(other.begin(),other.end(),99);
        assert(jfx_scene3d_render_shaded(scene,0,w,h,shading,other.data(),other.size())!=JFX_SUCCESS);
        for (auto byte:other) assert(byte==99);
        // The same driver succeeds at a valid time, including cloner sampling.
        assert(jfx_scene3d_command(scene,"3d.cloner",0,1,2,1,"")==JFX_SUCCESS);
        assert(jfx_scene3d_render_shaded(scene,1,w,h,shading,other.data(),other.size())==JFX_SUCCESS && coverage(other)>0);
    }
    jfx_scene3d_destroy(scene);
}
