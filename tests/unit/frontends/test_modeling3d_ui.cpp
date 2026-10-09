#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
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
    ImGui::DebugLocateItem(index<0?w->GetID(label):ImHashStr(label,0,w->GetID(index))); draw(f);
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
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_MODELING3D)==JFX_SUCCESS); draw(f); draw(f); draw(f);
    click(f,"Add cube");
    char state[16384]; assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS);
    assert(std::strstr(state,"\"name\":\"cube\"") && std::strstr(state,"\"active\":true"));
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
    jfx_desktop_frontend_destroy(f);
}
