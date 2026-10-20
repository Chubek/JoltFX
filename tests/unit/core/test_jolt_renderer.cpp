#include "jfx/jfx_modeling3d.h"
#include "jfx/jfx_image.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>

int main(int argc,char **argv) {
    auto *s=jfx_scene3d_create(); assert(s);
    assert(jfx_scene3d_command(s,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    jfx_scene3d_render_options_t o={}; o.size=sizeof(o); o.renderer=JFX_SCENE3D_RENDER_JOLT_CPU;
    o.samples=4; o.bounces=2; o.timeout_ms=10000; o.reflectivity=.15f;
    tilly::vector<uint8_t> a(47*31*4,99),b(a.size(),99);
    if (!jfx_scene3d_renderer_available(o.renderer)) {
        assert(jfx_scene3d_render_ex(s,0,47,31,&o,a.data(),a.size())==JFX_ERROR_NOT_IMPLEMENTED);
        for (auto p:a) assert(p==99);
    } else {
        assert(jfx_scene3d_render_ex(s,0,47,31,&o,a.data(),a.size())==JFX_SUCCESS);
        auto base=o; base.renderer=JFX_SCENE3D_RENDER_MONKEYTRACE;
        assert(jfx_scene3d_render_ex(s,0,47,31,&base,b.data(),b.size())==JFX_SUCCESS && a==b);
        assert(jfx_scene3d_command(s,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
        assert(jfx_scene3d_command(s,"3d.key",0,0,30,3,"")==JFX_SUCCESS);
        auto *snapshot=jfx_scene3d_clone(s); assert(snapshot);
        assert(jfx_scene3d_render_ex(snapshot,1,47,31,&o,b.data(),b.size())==JFX_SUCCESS && a!=b);
        assert(jfx_scene3d_command(s,"3d.color",0,0,0,.95,"")==JFX_SUCCESS);
        if (argc>1) {
            assert(jfx_scene3d_write_png_ex(snapshot,1,47,31,&o,argv[1])==JFX_SUCCESS);
            jfx_image_t image={}; image.size=sizeof(image);
            assert(jfx_image_load(argv[1],nullptr,&image)==JFX_SUCCESS);
            assert(!std::memcmp(image.pixels,b.data(),b.size()));
            jfx_image_release(&image); std::remove(argv[1]);
        }
        jfx_scene3d_destroy(snapshot);
    }
    o.renderer=JFX_SCENE3D_RENDER_JOLT_GPU;
    std::fill(a.begin(),a.end(),99);
    auto result=jfx_scene3d_render_ex(s,0,47,31,&o,a.data(),a.size());
    std::printf("Jolt Renderer GPU: %s\n",jfx_result_to_string(result));
    if (jfx_scene3d_renderer_available(o.renderer)) {
        assert(result==JFX_SUCCESS || result==JFX_ERROR_BACKEND_FAILURE);
        if (result==JFX_SUCCESS) {
            auto base=o; base.renderer=JFX_SCENE3D_RENDER_VULKAN;
            assert(jfx_scene3d_render_ex(s,0,47,31,&base,b.data(),b.size())==JFX_SUCCESS && a==b);
        }
    } else assert(result==JFX_ERROR_NOT_IMPLEMENTED);
    if (result!=JFX_SUCCESS) for (auto p:a) assert(p==99);
    jfx_scene3d_destroy(s);
}
