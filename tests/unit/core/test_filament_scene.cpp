#include "jfx/jfx_modeling3d.h"
#include "jfx/jfx_image.h"
#include "tilly/memory.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static double centroid_y(const tilly::vector<uint8_t> &pixels,const tilly::vector<uint8_t> &background) {
    double sum=0,count=0;
    for (size_t i=0;i<pixels.size();i+=4) {
        int difference=0; for (size_t c=0;c<3;++c) difference+=std::abs(int(pixels[i+c])-int(background[i+c]));
        if (difference>30) { sum+=double(i/4/96); ++count; }
    }
    assert(count>0); return sum/count;
}

int main(int argc,char **argv) {
    auto *scene=jfx_scene3d_create(); assert(scene);
    jfx_scene3d_render_options_t options={}; options.size=sizeof(options);
    options.renderer=JFX_SCENE3D_RENDER_FILAMENT;
    tilly::vector<uint8_t> a(96*64*4,99),b(a.size(),99);
    assert(jfx_scene3d_render_ex(nullptr,0,96,64,&options,a.data(),a.size())==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_scene3d_render_ex(scene,0,96,64,&options,a.data(),a.size()-1)==JFX_ERROR_INVALID_ARGUMENT);
    if (!jfx_scene3d_renderer_available(options.renderer)) {
        assert(jfx_scene3d_render_ex(scene,0,96,64,&options,a.data(),a.size())==JFX_ERROR_NOT_IMPLEMENTED);
        for (auto v:a) assert(v==99);
        jfx_scene3d_destroy(scene); return 0;
    }
    assert(jfx_scene3d_render_ex(scene,0,96,64,&options,a.data(),a.size())==JFX_SUCCESS);
    auto background=a;
    assert(jfx_scene3d_command(scene,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    assert(jfx_scene3d_render_ex(scene,0,96,64,&options,b.data(),b.size())==JFX_SUCCESS && a!=b);
    for (size_t i=3;i<b.size();i+=4) assert(b[i]==255);
    auto *snapshot=jfx_scene3d_clone(scene); assert(snapshot);
    auto center=centroid_y(b,background);
    assert(jfx_scene3d_command(scene,"3d.transform",0,1,0,1,"")==JFX_SUCCESS);
    assert(jfx_scene3d_render_ex(scene,0,96,64,&options,a.data(),a.size())==JFX_SUCCESS);
    assert(centroid_y(a,background)<center); // normalized top-left RGBA readback
    assert(jfx_scene3d_command(scene,"3d.transform",0,1,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(scene,"3d.color",0,0,0,.95,"")==JFX_SUCCESS);
    assert(jfx_scene3d_render_ex(scene,0,96,64,&options,a.data(),a.size())==JFX_SUCCESS && a!=b);
    assert(jfx_scene3d_render_ex(snapshot,0,96,64,&options,a.data(),a.size())==JFX_SUCCESS && a==b);
    assert(jfx_scene3d_command(scene,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(scene,"3d.key",0,0,30,3,"")==JFX_SUCCESS);
    assert(jfx_scene3d_render_ex(scene,1,96,64,&options,a.data(),a.size())==JFX_SUCCESS && a!=b);
    assert(jfx_scene3d_command(snapshot,"3d.cloner",0,2,3,2,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(snapshot,"3d.orbit",0,0,0,0,"10 15 20")==JFX_SUCCESS);
    assert(jfx_scene3d_render_ex(snapshot,0,96,64,&options,a.data(),a.size())==JFX_SUCCESS && a!=b);
    if (argc>1) {
        assert(jfx_scene3d_write_png_ex(snapshot,0,96,64,&options,argv[1])==JFX_SUCCESS);
        jfx_image_t image={}; image.size=sizeof(image);
        assert(jfx_image_load(argv[1],nullptr,&image)==JFX_SUCCESS);
        assert(image.width==96 && image.height==64 && !std::memcmp(image.pixels,a.data(),a.size()));
        jfx_image_release(&image); std::remove(argv[1]);
    }
    jfx_scene3d_destroy(snapshot); jfx_scene3d_destroy(scene);
}
