#include "jfx/jfx_modeling3d.h"
#include "modeling3d_preview.h"
#include "tilly/memory.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
int main() {
    auto *s=jfx_scene3d_create();
    auto edit=[&](const char *op,unsigned a,unsigned b,unsigned c,double v,const char *text) {
        if (jfx_scene3d_command(s,op,a,b,c,v,text)!=JFX_SUCCESS) std::abort();
    };
    edit("3d.add",64,0,0,0,"sphere"); edit("3d.cloner",0,2,8,2,"");
    tilly::vector<uint8_t> pixels(640*360*4);
    auto start=std::chrono::steady_clock::now();
    for (unsigned i=0;i<20;++i) if (jfx_scene3d_render(s,double(i)/30,640,360,pixels.data(),pixels.size())!=JFX_SUCCESS) std::abort();
    std::printf("3D 640x360, eight spheres: %.3f ms/frame\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20);
    const float black[3]={0,0,0};
    start=std::chrono::steady_clock::now();
    for (unsigned i=0;i<20;++i) if (jfx::detail::scene3d_render_preview(s,double(i)/30,320,180,
        JFX_SCENE3D_SHADING_GOURAUD,black,false,pixels.data(),pixels.size())!=JFX_SUCCESS) std::abort();
    std::printf("Interactive 320x180, eight spheres: %.3f ms/frame\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20);
    edit("3d.navigation_begin",0,0,0,0,""); start=std::chrono::steady_clock::now();
    for (unsigned i=0;i<100;++i) edit("3d.orbit",0,0,0,0,"1 1 0");
    std::printf("Camera: %.3f ms/update\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100);
    start=std::chrono::steady_clock::now(); edit("3d.navigation_end",0,0,0,0,"");
    std::printf("Camera gesture release: %.3f ms\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
    jfx_scene3d_destroy(s);
}
