#include "tools.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace jfx::ez;
int main(){
    Controller c;c.camera.width=1000;c.camera.height=700;c.camera.view.pitch=90;c.camera.view.orthographic=true;
    auto screen=[&](Vec p){glm::dvec2 result;assert(c.camera.project(p,result));return result;};
    c.set_tool(Tool::Rectangle);assert(c.click(screen({0,0,0})));c.move(screen({4,0,-3}));assert(c.preview_valid);
    auto expected=c.preview_mesh.triangles;assert(c.confirm());assert(expected.size()==c.index.mesh.triangles.size());
    for(size_t i=0;i<expected.size();++i)for(size_t j=0;j<3;++j)assert(glm::length(expected[i].points[j]-c.index.mesh.triangles[i].points[j])<1e-10);
    // Deterministic inference and hysteresis in screen pixels, independent of zoom.
    auto endpoint=c.inference.resolve(c.editor.doc,1,c.index,c.camera,screen({0,0,0}),Plane{});assert(endpoint.kind==SnapKind::Endpoint);
    auto pixel=screen({0,0,0});pixel.x+=10;auto stable=c.inference.resolve(c.editor.doc,1,c.index,c.camera,pixel,Plane{});assert(stable.kind==SnapKind::Endpoint && stable.id==endpoint.id);
    auto midpoint=c.inference.resolve(c.editor.doc,1,c.index,c.camera,screen({2,0,0}),Plane{});assert(midpoint.kind==SnapKind::Midpoint);
    auto hit=c.index.intersect(c.camera.ray(screen({2,0,-1})));assert(hit.entity.kind==Kind::Face && hit.path.empty());
    c.set_tool(Tool::Measure);assert(c.click(screen({0,0,0})));c.move(screen({4,0,0}));assert(c.preview_valid && !c.has_geometry_preview());c.cancel();
    // Exact length resolves preview and commit from the same anchored point.
    c.set_tool(Tool::Line);assert(c.click(screen({0,0,0})));std::snprintf(c.measurement,sizeof(c.measurement),"1 m");c.axis=1;c.move(screen({3,0,1}));
    assert(std::abs(glm::length(c.pending.points[1]-c.pending.points[0])-1)<1e-9);assert(c.confirm());c.cancel();
    c.set_tool(Tool::Line);assert(c.click(screen({0,0,0})));std::snprintf(c.measurement,sizeof(c.measurement),"not a length");c.move(screen({1,0,-1}));assert(!c.preview_valid);assert(!c.confirm());c.cancel();
    // Preview errors, cancellation and invalid numeric input never consume undo.
    auto revision=c.editor.revision;c.set_tool(Tool::Pull);assert(c.click(screen({2,0,-1})));std::snprintf(c.measurement,sizeof(c.measurement),"invalid");c.move(screen({2,0,-2}));assert(!c.preview_valid);assert(!c.confirm());c.cancel();assert(c.editor.revision==revision);
    // Camera projection/ray consistency in perspective and orthographic modes.
    for(bool orthographic:{false,true}){c.camera.view.orthographic=orthographic;c.camera.view.pitch=35;Vec point{1,0,-1};auto pixel2=screen(point);Vec on;assert(c.camera.on_plane(pixel2,Plane{},on));assert(glm::length(on-point)<1e-9);
        auto before=screen(point);c.camera.zoom(1,before,&point);assert(glm::length(screen(point)-before)<1e-8);}
    // Index publication removes stale IDs after deletion/undo.
    Command remove;remove.op=Op::Delete;remove.entity=c.editor.doc.scopes[0].faces[0].id;assert(c.action(remove));
    assert(c.index.intersect(c.camera.ray(screen({2,0,-1}))).entity.kind==Kind::None);c.history(false);assert(!c.index.mesh.triangles.empty());
    for(Tool tool:{Tool::Circle,Tool::Arc}){
        Controller curve;curve.camera=c.camera;curve.camera.view=View{};curve.camera.view.pitch=90;curve.camera.view.orthographic=true;
        auto curve_pixel=[&](Vec p){glm::dvec2 out;assert(curve.camera.project(p,out));return out;};
        curve.set_tool(tool);assert(curve.click(curve_pixel({0,0,0})));
        if(tool==Tool::Arc)assert(curve.click(curve_pixel({1,0,0})));
        curve.move(curve_pixel(tool==Tool::Arc?Vec(0,0,-1):Vec(1,0,0)));assert(curve.preview_valid);assert(curve.confirm());
        assert(curve.editor.doc.scopes[0].edges.size()==32);assert(curve.editor.doc.scopes[0].faces.size()==(tool==Tool::Circle?1u:0u));
    }
}
