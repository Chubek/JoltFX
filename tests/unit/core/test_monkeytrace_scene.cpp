#include "jfx/jfx_modeling3d.h"
#include "tilly/memory.hpp"
#include "jfx/jfx_image.h"
#include <algorithm>
#include <cstdio>
#include <cassert>
#include <cstring>

int main(int argc,char **argv) {
 assert(!jfx_scene3d_clone(nullptr));
 auto *scene=jfx_scene3d_create(); assert(scene);
 assert(jfx_scene3d_command(scene,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
 jfx_scene3d_render_options_t options={}; options.size=sizeof(options);
 options.renderer=JFX_SCENE3D_RENDER_MONKEYTRACE; options.samples=4; options.bounces=2;
 options.timeout_ms=10000; options.reflectivity=.15f;
 tilly::vector<uint8_t> a(64*48*4,99),b(a.size(),99);
 assert(jfx_scene3d_render_ex(nullptr,0,64,48,&options,a.data(),a.size())==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_scene3d_render_ex(scene,0,64,48,nullptr,a.data(),a.size())==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_scene3d_write_png_ex(scene,0,64,48,&options,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_scene3d_render_ex(scene,0,64,48,&options,a.data(),a.size()-1)==JFX_ERROR_INVALID_ARGUMENT);
 for (auto v:a) assert(v==99);
 if (!jfx_scene3d_renderer_available(JFX_SCENE3D_RENDER_MONKEYTRACE)) {
  assert(jfx_scene3d_render_ex(scene,0,64,48,&options,a.data(),a.size())==JFX_ERROR_NOT_IMPLEMENTED);
  for (auto v:a) assert(v==99);
  jfx_scene3d_destroy(scene); return 0;
 }
 assert(jfx_scene3d_render_ex(scene,0,64,48,&options,a.data(),a.size())==JFX_SUCCESS);
 for (size_t i=3;i<a.size();i+=4) assert(a[i]==255);
 auto *snapshot=jfx_scene3d_clone(scene); assert(snapshot);
 assert(jfx_scene3d_command(scene,"3d.color",0,0,0,.9,"")==JFX_SUCCESS);
 assert(jfx_scene3d_render_ex(snapshot,0,64,48,&options,b.data(),b.size())==JFX_SUCCESS && a==b);
 assert(jfx_scene3d_render_ex(scene,0,64,48,&options,b.data(),b.size())==JFX_SUCCESS && a!=b);
 assert(jfx_scene3d_command(scene,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
 assert(jfx_scene3d_command(scene,"3d.key",0,0,30,3,"")==JFX_SUCCESS);
 assert(jfx_scene3d_render_ex(scene,1,64,48,&options,a.data(),a.size())==JFX_SUCCESS && a!=b);
 // Cloner instances are real transformed triangles, not just the source mesh.
 assert(jfx_scene3d_command(snapshot,"3d.cloner",0,1,3,3,"")==JFX_SUCCESS);
 assert(jfx_scene3d_render_ex(snapshot,0,64,48,&options,a.data(),a.size())==JFX_SUCCESS && a!=b);
 if (argc>1) {
  assert(jfx_scene3d_write_png_ex(snapshot,0,64,48,&options,argv[1])==JFX_SUCCESS);
  jfx_image_t image={}; image.size=sizeof(image);
  assert(jfx_image_load(argv[1],nullptr,&image)==JFX_SUCCESS && image.width==64 && image.height==48);
  assert(std::memcmp(image.pixels,a.data(),a.size())==0); jfx_image_release(&image); std::remove(argv[1]);
 }
 assert(jfx_scene3d_command(scene,"3d.add",0,0,0,0,"sphere")==JFX_SUCCESS);
 assert(jfx_scene3d_command(scene,"3d.cloner",1,1,64,1,"")==JFX_SUCCESS);
 std::fill(a.begin(),a.end(),99);
 assert(jfx_scene3d_render_ex(scene,0,64,48,&options,a.data(),a.size())==JFX_ERROR_BACKEND_FAILURE);
 for (auto v:a) assert(v==99);
 assert(jfx_scene3d_command(scene,"3d.visible",1,0,0,0,"")==JFX_SUCCESS);
 assert(jfx_scene3d_render_ex(scene,0,64,48,&options,a.data(),a.size())==JFX_SUCCESS);
 assert(jfx_scene3d_command(scene,"3d.script",0,1,0,0,"(defkernel lift [time frame index value] (+ value time))")==JFX_SUCCESS);
 assert(jfx_scene3d_render_ex(scene,.5,64,48,&options,b.data(),b.size())==JFX_SUCCESS && a!=b);
 assert(jfx_scene3d_command(scene,"3d.orbit",0,0,0,0,"10 15 20")==JFX_SUCCESS);
 assert(jfx_scene3d_render_ex(scene,.5,64,48,&options,a.data(),a.size())==JFX_SUCCESS && a!=b);
 options.samples=65; std::fill(a.begin(),a.end(),99);
 assert(jfx_scene3d_render_ex(scene,0,64,48,&options,a.data(),a.size())==JFX_ERROR_INVALID_ARGUMENT);
 for (auto v:a) assert(v==99);
 jfx_scene3d_destroy(snapshot); jfx_scene3d_destroy(scene);
}
