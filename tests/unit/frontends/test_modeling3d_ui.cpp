#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../../third_party/ImGuizmo/src/ImGuizmo.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cfloat>
#include <cstdio>
#include <algorithm>
static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static ImGuiWindow *window(const char *part) {
    for (auto *w:ImGui::GetCurrentContext()->Windows) if (w->Active && std::strstr(w->Name,part)) return w;
    std::fprintf(stderr,"Missing UI window: %s\n",part);
    for (auto *w:ImGui::GetCurrentContext()->Windows) std::fprintf(stderr,"%s (active=%d)\n",w->Name,int(w->Active));
    assert(false); return nullptr;
}
static void click(jfx_desktop_frontend_t *f,const char *label,const char *part="/Editor interface_",int index=-1) {
    auto *w=window(part); auto &io=ImGui::GetIO(); io.AddMousePosEvent(0,0); draw(f);
    bool subtab=!std::strcmp(label,"Design") || !std::strcmp(label,"Animation") || !std::strcmp(label,"Sculpting");
    ImGui::DebugLocateItem(subtab?ImHashStr(label,0,w->GetID("3D modes")):
        index<0?w->GetID(label):ImHashStr(label,0,w->GetID(index))); draw(f);
    ImRect bounds=w->InnerRect; bounds.Expand(4); ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    for (auto &v:ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x); lo.y=std::fmin(lo.y,v.pos.y); hi.x=std::fmax(hi.x,v.pos.x); hi.y=std::fmax(hi.y,v.pos.y);
    }
    if (lo.x>hi.x) std::fprintf(stderr,"Missing widget: %s in %s\n",label,w->Name);
    assert(lo.x<=hi.x); io.AddMousePosEvent((lo.x+hi.x)/2,(lo.y+hi.y)/2); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f); io.AddMouseButtonEvent(0,false); draw(f); draw(f);
}
int main() {
    jfx_desktop_frontend_config_t config={}; config.size=sizeof(config); config.width=1440; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    jfx_scene3d_render_options_t raster={}; raster.size=sizeof(raster); raster.renderer=JFX_SCENE3D_RENDER_RASTER;
    assert(jfx_desktop_frontend_set_scene3d_render_options(f,&raster)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_MODELING3D)==JFX_SUCCESS); draw(f); draw(f); draw(f);
    click(f,"Add cube");
    char state[16384]; assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS);
    assert(std::strstr(state,"\"name\":\"cube\"") && std::strstr(state,"\"active\":true"));
    // Grab ImGuizmo's screen-translation handle, not the navigation canvas.
    // Live edits must move the mesh without orbiting, then undo in one step.
    {
        auto *v=window("3D viewport"); auto &input=ImGui::GetIO();
        input.AddMousePosEvent(0,0); draw(f);
        ImRect rect(FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX);
        for (auto &vertex:v->DrawList->VtxBuffer) if (vertex.col==IM_COL32(0,0,0,255)) rect.Add(vertex.pos);
        assert(rect.GetWidth()>100); auto center=rect.GetCenter();
        unsigned char original[96*96*4],moved[sizeof(original)];
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,original,sizeof(original))==JFX_SUCCESS);
        input.AddMousePosEvent(center.x,center.y); draw(f);
        input.AddMouseButtonEvent(0,true); draw(f);
        assert(ImGuizmo::IsUsing());
        for (int i=1;i<=4;++i) { input.AddMousePosEvent(center.x+float(i)*12,center.y); draw(f); }
        input.AddMouseButtonEvent(0,false); draw(f); draw(f);
        assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS);
        assert(!std::strstr(state,"\"transform\":[0,0,0,0,0,0,1,1,1]"));
        assert(std::strstr(state,"\"camera\":[35,22,7,0,0,0,45]"));
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))!=0);
        click(f,"Undo 3D");
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))==0);
        input.AddMousePosEvent(center.x,center.y); draw(f);
        input.AddMouseButtonEvent(0,true); draw(f);
        input.AddMousePosEvent(center.x+30,center.y); draw(f);
        input.AddKeyEvent(ImGuiKey_Escape,true); draw(f); input.AddKeyEvent(ImGuiKey_Escape,false);
        input.AddMouseButtonEvent(0,false); draw(f); draw(f);
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))==0);
        click(f,"Gizmo","3D viewport"); click(f,"Scale","##Combo_",2);
        input.AddMousePosEvent(center.x,center.y); draw(f);
        input.AddMouseButtonEvent(0,true); draw(f); assert(ImGuizmo::IsUsing());
        input.AddMousePosEvent(center.x+40,center.y); draw(f);
        input.AddMouseButtonEvent(0,false); draw(f); draw(f);
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))!=0);
        click(f,"Undo 3D");
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))==0);
        click(f,"Gizmo","3D viewport"); click(f,"Move","##Combo_",0);
        // Hovering an object handle must not swallow camera wheel input.
        input.AddMousePosEvent(center.x,center.y); draw(f);
        float scroll=v->Scroll.y;
        input.AddMouseWheelEvent(0,1); draw(f);
        assert(v->Scroll.y==scroll);
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))!=0);
        input.AddKeyEvent(ImGuiKey_Escape,true); draw(f); input.AddKeyEvent(ImGuiKey_Escape,false); draw(f);
        assert(jfx_desktop_frontend_render_rgba8(f,96,96,moved,sizeof(moved))==JFX_SUCCESS && std::memcmp(original,moved,sizeof(moved))==0);
    }
    // Nested views share the document, selection and history without adding
    // top-level workspace IDs or duplicating the scene.
    click(f,"Animation");
    click(f,"Key","3D inspector",0);
    assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS);
    assert(std::strstr(state,"\"name\":\"cube\""));
    click(f,"Undo 3D"); // remove the key, not the cube
    click(f,"Design");
    // The windowed viewport requests its display size, not the legacy 320x180
    // effect-preview size. Exercise landscape, portrait and high-DPI rasters.
    for (auto size:{ImVec2(960,540),ImVec2(320,480),ImVec2(1280,720)}) {
        uint32_t width=uint32_t(size.x),height=uint32_t(size.y);
        tilly::vector<uint8_t> pixels(size_t(width)*height*4,99);
        assert(jfx_desktop_frontend_render_rgba8(f,width,height,pixels.data(),pixels.size())==JFX_SUCCESS);
        size_t visible=0;
        for (size_t i=0;i<pixels.size();i+=4) {
            assert(pixels[i+3]==255);
            if (pixels[i]!=22 || pixels[i+1]!=26 || pixels[i+2]!=34) ++visible;
        }
        assert(visible>size_t(width)*height/100);
        std::fill(pixels.begin(),pixels.end(),99);
        assert(jfx_desktop_frontend_render_rgba8(f,width,height,pixels.data(),pixels.size()-1)==JFX_ERROR_INVALID_ARGUMENT);
        for (auto p:pixels) assert(p==99);
        assert(jfx_desktop_frontend_render_rgba8(f,2049,1,pixels.data(),pixels.size())==JFX_ERROR_INVALID_ARGUMENT && pixels[0]==99);
    }
    unsigned char before[128*128*4],after[sizeof(before)];
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,before,sizeof(before))==JFX_SUCCESS);
    // Inspection shading is viewport-local, not an object edit or an export
    // renderer change. Drive the actual combo rather than a test-only setter.
    for (auto mode:{1,2,0}) {
        click(f,"Viewport shading","3D viewport");
        click(f,mode==1?"Gouraud Shading":mode==2?"Wireframe Shading":"Rendered","##Combo_",mode);
        assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS && std::memcmp(before,after,sizeof(before))==0);
    }
    click(f,"Undo 3D"); assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS); assert(std::strstr(state,"\"objects\":[]"));
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS); assert(std::memcmp(before,after,sizeof(before))!=0);
    click(f,"Redo 3D"); assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS); assert(std::strstr(state,"\"name\":\"cube\""));
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS); draw(f);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_MODELING3D)==JFX_SUCCESS); draw(f); draw(f); draw(f);
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS); assert(std::memcmp(before,after,sizeof(before))==0);
    // Navigate the actual headless viewport item: multiple mouse updates must
    // change the camera but create only one history entry on release.
    auto *viewport=window("3D viewport"); auto &io=ImGui::GetIO();
    ImVec2 point(viewport->InnerRect.Min.x+70,viewport->InnerRect.Max.y-120);
    io.AddMousePosEvent(point.x,point.y); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f);
    for (int i=1;i<=4;++i) { io.AddMousePosEvent(point.x+float(i)*15,point.y+float(i)*5); draw(f); }
    io.AddMouseButtonEvent(0,false); draw(f); draw(f);
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS);
    assert(std::memcmp(before,after,sizeof(before))!=0);
    assert(jfx_desktop_frontend_edit(f,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS && std::memcmp(before,after,sizeof(before))==0);
    assert(jfx_desktop_frontend_edit(f,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"objects\":[]"));
    click(f,"Add cube");
    int primitive=8;
    for (auto kind:{"tube","hemisphere","wedge","tetrahedron","octahedron","icosahedron"}) {
        click(f,"More primitives"); click(f,kind,"##Combo_",primitive++); click(f,"Add primitive");
        assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,kind));
        // Creation selects the new object, so Delete removes it rather than
        // the pre-existing cube. Undo/redo must preserve both meshes.
        click(f,"Delete mesh","3D inspector");
        assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,kind) && std::strstr(state,"\"name\":\"cube\""));
        click(f,"Undo 3D");
        assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,kind));
        click(f,"Redo 3D");
    }
    for (auto size:{ImVec2(1280,720),ImVec2(640,480)}) {
        assert(jfx_desktop_frontend_resize(f,uint32_t(size.x),uint32_t(size.y))==JFX_SUCCESS);
        draw(f); draw(f); draw(f);
        auto *v=window("3D viewport");
        assert(!v->Hidden && v->InnerClipRect.GetWidth()>200 && v->InnerClipRect.GetHeight()>150);
        assert(v->InnerClipRect.Min.x>=0 && v->InnerClipRect.Max.x<=size.x && v->InnerClipRect.Max.y<=size.y);
    }
    // Real Sculpting tab and viewport input, with grouped undo and Escape.
    assert(jfx_desktop_frontend_resize(f,1440,1000)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.new",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.add",32,0,0,0,"sphere")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.view",0,0,0,0,"")==JFX_SUCCESS);
    draw(f); draw(f); click(f,"Sculpting"); draw(f);
    auto *v=window("3D viewport");
    ImGui::SetScrollY(v,0); draw(f); draw(f);
    io.AddMousePosEvent(0,0); draw(f);
    ImGui::DebugLocateItem(v->GetID("##3D navigation")); draw(f);
    ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    ImRect viewport_bounds=v->InnerRect; viewport_bounds.Expand(4);
    for (auto &vertex:ImGui::GetForegroundDrawList()->VtxBuffer) if (viewport_bounds.Contains(vertex.pos)) {
        lo.x=std::min(lo.x,vertex.pos.x); lo.y=std::min(lo.y,vertex.pos.y);
        hi.x=std::max(hi.x,vertex.pos.x); hi.y=std::max(hi.y,vertex.pos.y);
    }
    if (!(lo.x<hi.x)) std::fprintf(stderr,"Sculpt viewport %s: inner %.0f %.0f %.0f %.0f scroll %.0f vertices %d\n",v->Name,
        double(v->InnerRect.Min.x),double(v->InnerRect.Min.y),double(v->InnerRect.Max.x),double(v->InnerRect.Max.y),double(v->Scroll.y),ImGui::GetForegroundDrawList()->VtxBuffer.Size);
    assert(lo.x<hi.x); ImVec2 center((lo.x+hi.x)/2,(lo.y+hi.y)/2);
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,before,sizeof(before))==JFX_SUCCESS);
    auto stroke=[&](bool cancel) {
        io.AddMousePosEvent(center.x,center.y); draw(f); io.AddMouseButtonEvent(0,true); draw(f);
        for (int i=1;i<5;++i) { io.AddMousePosEvent(center.x+float(i)*3,center.y); draw(f); }
        if (cancel) { io.AddKeyEvent(ImGuiKey_Escape,true); draw(f); io.AddKeyEvent(ImGuiKey_Escape,false); }
        io.AddMouseButtonEvent(0,false); draw(f); draw(f);
    };
    stroke(false);
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS && std::memcmp(before,after,sizeof(before))!=0);
    click(f,"Undo 3D");
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS && std::memcmp(before,after,sizeof(before))==0);
    stroke(true);
    assert(jfx_desktop_frontend_render_rgba8(f,128,128,after,sizeof(after))==JFX_SUCCESS && std::memcmp(before,after,sizeof(before))==0);
    for (auto label:{"Low-Poly","Mid-Poly","High-Poly"}) {
        click(f,label,"3D inspector");
        assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,label));
        click(f,"Undo 3D");
        assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,label));
    }
    click(f,"Design");
    click(f,"Mesh editing","3D inspector");
    click(f,"Optimize mesh (meshoptimizer)","3D inspector"); click(f,"Undo 3D");
    click(f,"Convex hull (Bullet)","3D inspector"); click(f,"Undo 3D");
    click(f,"Animation");
    click(f,"Motion path (GLM)","3D inspector");
    click(f,"Bake Catmull-Rom path","3D inspector");
    assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"frame\":59"));
    click(f,"Undo 3D");
    assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"keys\":[]"));
    assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"sphere"));
    jfx_desktop_frontend_destroy(f);
}
