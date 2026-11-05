#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static ImRect canvas() {
    for (auto *w:ImGui::GetCurrentContext()->Windows) if (w->Active &&
        (std::strstr(w->Name,"3D viewport") || std::strstr(w->Name,"/Editor interface_"))) {
        ImRect rect(1.e6f,1.e6f,-1.e6f,-1.e6f);
        for (const auto &v:w->DrawList->VtxBuffer) if (v.col==IM_COL32(0,0,0,255)) rect.Add(v.pos);
        if (rect.GetWidth()>100 && rect.GetHeight()>100) return rect;
    }
    assert(false); return {};
}
static std::array<float,4> quaternion(jfx_desktop_frontend_t *f) {
    char state[4096]; assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS);
    auto *q=std::strstr(state,"\"quaternion\":["); assert(q); std::array<float,4> result;
    assert(std::sscanf(q,"\"quaternion\":[%f,%f,%f,%f",&result[0],&result[1],&result[2],&result[3])==4); return result;
}
static float distance(jfx_desktop_frontend_t *f) {
    char state[4096]; assert(jfx_desktop_frontend_scene3d_state(f,state,sizeof(state))==JFX_SUCCESS);
    auto *c=std::strstr(state,"\"camera\":["); float yaw,pitch,d;
    assert(c && std::sscanf(c,"\"camera\":[%f,%f,%f",&yaw,&pitch,&d)==3); return d;
}
static bool same(const std::array<float,4> &a,const std::array<float,4> &b) {
    for (size_t i=0;i<4;++i) if (std::abs(a[i]-b[i])>2.e-5f) return false;
    return true;
}
static void edit(jfx_desktop_frontend_t *f,const char *op) { assert(jfx_desktop_frontend_edit(f,op,0,0,0,0,"")==JFX_SUCCESS); }
static void drag(jfx_desktop_frontend_t *f,ImVec2 start,int count,float step) {
    auto &io=ImGui::GetIO(); io.AddMousePosEvent(start.x,start.y); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f);
    for (int i=1;i<=count;++i) { io.AddMousePosEvent(start.x+float(i)*step,start.y); draw(f); }
    io.AddMouseButtonEvent(0,false); draw(f); draw(f);
}
int main() {
    jfx_desktop_frontend_config_t config{}; config.size=sizeof(config);
    config.width=1440; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    for (auto workspace:{JFX_DESKTOP_WORKSPACE_MODELING3D,JFX_DESKTOP_WORKSPACE_GAME}) {
        assert(jfx_desktop_frontend_set_workspace(f,workspace)==JFX_SUCCESS);
        edit(f,"3d.new"); draw(f); draw(f); draw(f);
        auto bounds=canvas(); ImVec2 point(bounds.Min.x+80,bounds.Min.y+bounds.GetHeight()*.65f);
        auto baseline=quaternion(f);
        drag(f,point,1,120); auto fast=quaternion(f); assert(!same(baseline,fast));
        edit(f,"undo"); assert(same(quaternion(f),baseline));
        drag(f,point,12,10); assert(same(quaternion(f),fast)); // no lost >10px deltas
        edit(f,"undo"); assert(same(quaternion(f),baseline));
        auto &io=ImGui::GetIO(); io.AddMousePosEvent(point.x,point.y); draw(f);
        float before=distance(f);
        for (int i=0;i<3;++i) { io.AddMouseWheelEvent(0,1); draw(f); }
        float after=distance(f); assert(std::abs(after-before*std::exp(-.9f))<.001f);
        for (int i=0;i<15;++i) draw(f); // scroll burst ends as one undo entry
        edit(f,"undo"); assert(std::abs(distance(f)-before)<.001f);
        edit(f,"redo"); assert(std::abs(distance(f)-after)<.001f);
        edit(f,"undo");
        // Gimbal tangent drags must rotate even on a 45-degree diagonal (the
        // former dx-dy mapping cancelled such movement entirely).
        bounds=canvas(); ImVec2 center(bounds.Max.x-65,bounds.Min.y+65),ring;
        bool found=false;
        for (auto *w:ImGui::GetCurrentContext()->Windows) if (w->Active)
            for (const auto &v:w->DrawList->VtxBuffer) if (v.col==IM_COL32(240,85,75,255) &&
                v.pos.x>center.x-42 && v.pos.x<center.x-15 && v.pos.y>center.y-42 && v.pos.y<center.y-15) { ring=v.pos; found=true; }
        assert(found);
        io.AddMousePosEvent(ring.x,ring.y); draw(f); io.AddMouseButtonEvent(0,true); draw(f);
        io.AddMousePosEvent(ring.x+30,ring.y+30); draw(f);
        assert(!same(quaternion(f),baseline));
        io.AddKeyEvent(ImGuiKey_Escape,true); draw(f); io.AddKeyEvent(ImGuiKey_Escape,false);
        io.AddMouseButtonEvent(0,false); draw(f); draw(f); assert(same(quaternion(f),baseline));
    }
    jfx_desktop_frontend_destroy(f);
}
