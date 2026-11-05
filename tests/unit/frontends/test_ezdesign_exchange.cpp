#include "model.h"
#include "modeling3d_preview.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace jfx::ez;
int main(){
    Editor e;Command c;c.op=Op::Rectangle;c.points={{-2,0,2},{2,0,-2}};assert(e.execute(c));
    c=Command{};c.op=Op::Extrude;c.entity=e.doc.scopes[0].faces[0].id;c.value=.3;assert(e.execute(c));
    c=Command{};c.op=Op::Opening;c.entity=e.doc.scopes[0].faces[0].id;c.points={{-.5,.3,.5},{.5,.3,-.5}};assert(e.execute(c));
    assert(e.export_ply("ezdesign-exchange.ply"));
    // Independent reader: the existing Core/tinyply importer, not EZDesign's
    // serializer or triangulator. The exported opening remains open geometry.
    auto *scene=jfx_scene3d_create();assert(scene);
    assert(jfx_scene3d_command(scene,"3d.import_ply",0,0,0,0,"ezdesign-exchange.ply")==JFX_SUCCESS);
    jfx_object3d_info_t info{};info.size=sizeof(info);assert(jfx_scene3d_object_info(scene,0,&info)==JFX_SUCCESS);assert(info.triangles==32 && info.vertices==96);
    jfx_scene3d_destroy(scene);
    // Same z-buffered editing raster as Design: orthographic opening center is
    // black while the surrounding cap is covered. Perspective exercises the
    // same adapter without changing the public scene renderer's contract.
    auto mesh=triangulate(e.doc);tilly::vector<jfx::detail::EditingTriangle> triangles;
    for(auto &t:mesh.triangles){jfx::detail::EditingTriangle out;for(size_t i=0;i<3;++i){auto p=t.points[i];out.positions[i]={float(p.x),float(p.y),float(p.z)};}out.color={.5f,.7f,.9f};triangles.push_back(out);}
    const float quaternion[]={-.70710678f,0,0,.70710678f},background[]={0,0,0};tilly::vector<uint8_t> pixels(256*256*4,77);
    for(float orthographic_height:{6.f,0.f}){
        assert(jfx::detail::render_editing_triangles(triangles.data(),triangles.size(),quaternion,8,orthographic_height,256,256,background,false,pixels.data(),pixels.size())==JFX_SUCCESS);
        size_t center=(128*256+128)*4;assert(pixels[center]==0 && pixels[center+1]==0 && pixels[center+2]==0);
        size_t cap=(128*256+190)*4;assert(pixels[cap+1]>0);
    }
    std::fill(pixels.begin(),pixels.end(),77);triangles[0].positions[0][0]=NAN;
    assert(jfx::detail::render_editing_triangles(triangles.data(),triangles.size(),quaternion,8,6,256,256,background,false,pixels.data(),pixels.size())==JFX_ERROR_INVALID_ARGUMENT);
    for(auto byte:pixels)assert(byte==77);
    std::remove("ezdesign-exchange.ply");
}
