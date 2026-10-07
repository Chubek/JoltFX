/* Drive the real animation canvas without a window or graphics driver. */
#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cfloat>
static void frame(jfx_desktop_frontend_t *f) {assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS);}
static void move(jfx_desktop_frontend_t *f,ImVec2 p) {ImGui::GetIO().AddMousePosEvent(p.x,p.y);frame(f);}
static void mouse(jfx_desktop_frontend_t *f,bool down) {ImGui::GetIO().AddMouseButtonEvent(0,down);frame(f);}
static void key(jfx_desktop_frontend_t *f,ImGuiKey k,bool ctrl=false,bool shift=false) {
    auto &io=ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);io.AddKeyEvent(ImGuiMod_Shift,shift);
    io.AddKeyEvent(k,true);frame(f);io.AddKeyEvent(k,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);frame(f);
}
static ImGuiWindow *window(const char *part) {
    for(auto *w:ImGui::GetCurrentContext()->Windows) if(w->Active && std::strstr(w->Name,part)) return w;
    assert(false);return nullptr;
}
static ImVec2 locate(jfx_desktop_frontend_t *f,ImGuiWindow *w,const char *name) {
    move(f,{0,0}); ImGui::DebugLocateItem(w->GetID(name));frame(f);
    ImRect bounds=w->InnerRect;bounds.Expand(4);
    ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    for(auto &v:ImGui::GetForegroundDrawList()->VtxBuffer) if(bounds.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x);lo.y=std::fmin(lo.y,v.pos.y);hi.x=std::fmax(hi.x,v.pos.x);hi.y=std::fmax(hi.y,v.pos.y);
    }
    assert(lo.x<=hi.x);return {(lo.x+hi.x)/2,(lo.y+hi.y)/2};
}
static void click(jfx_desktop_frontend_t *f,ImVec2 p) {move(f,p);mouse(f,true);mouse(f,false);frame(f);}
static ImRect colored(ImU32 color) {
    ImRect r(FLT_MAX,FLT_MAX,-FLT_MAX,-FLT_MAX);
    auto *data=ImGui::GetDrawData();
    auto *stage=window("/Vector stage_");
    for(int i=0;i<data->CmdListsCount;++i) if(data->CmdLists[i]==stage->DrawList)
        for(auto &v:data->CmdLists[i]->VtxBuffer) if(v.col==color) r.Add(v.pos);
    return r;
}
int main() {
    jfx_desktop_frontend_config_t config{};config.size=sizeof(config);config.width=1280;config.height=1000;config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr;assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_ANIMATION)==JFX_SUCCESS);
    frame(f);frame(f);frame(f);
    ImRect stage=colored(IM_COL32(250,249,246,255));assert(stage.GetWidth()>400);
    click(f,locate(f,window("/Drawing tools_"),"Rectangle (R)"));
    ImVec2 a(stage.Min.x+stage.GetWidth()*.2f,stage.Min.y+stage.GetHeight()*.2f);
    ImVec2 b(a.x+100,a.y+80);
    move(f,a);mouse(f,true);move(f,b);mouse(f,false);
    ImU32 fill=ImGui::ColorConvertFloat4ToU32(ImVec4(.31f,.63f,.88f,1));
    ImRect drawn=colored(fill);assert(drawn.GetWidth()>95 && drawn.GetHeight()>75);
    key(f,ImGuiKey_Z,true);assert(colored(fill).Min.x==FLT_MAX);
    key(f,ImGuiKey_Z,true,true);assert(colored(fill).GetWidth()>95);
    key(f,ImGuiKey_V);
    ImVec2 middle((a.x+b.x)/2,(a.y+b.y)/2);
    move(f,middle);mouse(f,true);move(f,{middle.x+40,middle.y+20});mouse(f,false);
    ImRect moved=colored(fill);assert(std::abs(moved.Min.x-drawn.Min.x-40)<2);
    key(f,ImGuiKey_Z,true);assert(std::abs(colored(fill).Min.x-drawn.Min.x)<2);
    // Cancel a new shape without adding artwork or consuming undo history.
    key(f,ImGuiKey_R);move(f,{b.x+20,b.y+20});mouse(f,true);move(f,{b.x+80,b.y+80});key(f,ImGuiKey_Escape);mouse(f,false);
    assert(std::abs(colored(fill).Max.x-drawn.Max.x)<2);
    click(f,locate(f,window("/Editor interface_"),"Blank key"));
    assert(colored(fill).Min.x==FLT_MAX);
    key(f,ImGuiKey_Z,true);assert(colored(fill).GetWidth()>95);
    // Switching workspaces retains the drawing, but cancels in-flight gestures.
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS);frame(f);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_ANIMATION)==JFX_SUCCESS);frame(f);frame(f);
    assert(colored(fill).GetWidth()>95);
    jfx_desktop_frontend_destroy(f);
}
