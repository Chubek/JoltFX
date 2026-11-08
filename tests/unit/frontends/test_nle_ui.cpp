/* Real NLE script widgets: pixels, atomic failure, and one-step undo/redo. */
#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cmath>
#include <cstring>

static void frame(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static void move(jfx_desktop_frontend_t *f,ImVec2 p) { ImGui::GetIO().AddMousePosEvent(p.x,p.y); frame(f); }
static void button(jfx_desktop_frontend_t *f,bool down) { ImGui::GetIO().AddMouseButtonEvent(0,down); frame(f); }
static void edit(jfx_desktop_frontend_t *f,const char *op) { assert(jfx_desktop_frontend_edit(f,op,0,0,0,0,"")==JFX_SUCCESS); }
static void render(jfx_desktop_frontend_t *f,uint8_t out[4]) { assert(jfx_desktop_frontend_render_rgba8(f,1,1,out,4)==JFX_SUCCESS); }
static ImVec2 locate(jfx_desktop_frontend_t *f,ImGuiWindow *window,const char *label) {
    move(f,ImVec2(0,0)); ImGui::DebugLocateItem(window->GetID(label)); frame(f);
    ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    ImRect bounds=window->InnerRect; bounds.Expand(4);
    for (const auto &v:ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x); lo.y=std::fmin(lo.y,v.pos.y);
        hi.x=std::fmax(hi.x,v.pos.x); hi.y=std::fmax(hi.y,v.pos.y);
    }
    assert(lo.x<=hi.x && lo.y<=hi.y); return ImVec2((lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f);
}
int main() {
    jfx_desktop_frontend_config_t config{}; config.size=sizeof(config);
    config.width=1280; config.height=1600; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_edit(f,"sequence.new",4,2,30,1,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"clip.add",0,0,0,30,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_panel_visible(f,JFX_DESKTOP_PANEL_VIEWPORT,false)==JFX_SUCCESS);
    frame(f); frame(f);
    ImGuiWindow *nle=nullptr;
    for (auto *w:ImGui::GetCurrentContext()->Windows)
        if (w->Active && std::strstr(w->Name,"/Editor interface_")) { nle=w; break; }
    assert(nle);
    ImGui::GetCurrentContext()->NavNextActivateId=nle->GetID("Joltscript edits"); frame(f); frame(f);
    auto set_script=[&](const char *source) {
        ImVec2 p=locate(f,nle,"##NLE script"); move(f,p); button(f,true); button(f,false);
        auto &io=ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_A,true); frame(f);
        io.AddKeyEvent(ImGuiKey_A,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame(f);
        io.AddInputCharactersUTF8(source); frame(f);
        io.AddKeyEvent(ImGuiKey_Tab,true); frame(f); io.AddKeyEvent(ImGuiKey_Tab,false); frame(f);
    };
    set_script("(defkernel edit [] [[\"clip.name\" 0 0 0 0 \"Widget batch\"] [\"effect.add\" 0 0 0 0 \"invert\"]])");
    uint8_t before[4],after[4],restored[4]; render(f,before);
    ImGui::GetCurrentContext()->NavNextActivateId=nle->GetID("Run Joltscript edit"); frame(f); frame(f);
    render(f,after); assert(std::memcmp(before,after,4));
    char state[4096]; assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS);
    assert(std::strstr(state,"Widget batch") && std::strstr(state,"\"effects\":1"));
    edit(f,"undo"); render(f,restored); assert(!std::memcmp(before,restored,4));
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,"Widget batch"));
    set_script("(defkernel edit [] [[\"clip.name\" 0 0 0 0 \"Rejected\"] [\"clip.split\" 0 0 0 9999 \"\"]])");
    ImGui::GetCurrentContext()->NavNextActivateId=nle->GetID("Run Joltscript edit"); frame(f); frame(f);
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,"Rejected"));
    edit(f,"redo"); render(f,restored); assert(!std::memcmp(after,restored,4));
    edit(f,"undo");
    ImGui::GetCurrentContext()->NavNextActivateId=nle->GetID("Joltscript edits"); frame(f); frame(f);
    ImVec2 gain=locate(f,nle,"Track audio gain"); move(f,gain); button(f,true);
    for (int i=1;i<=4;++i) move(f,ImVec2(gain.x+10*(float)i,gain.y));
    button(f,false);
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,"\"audioGain\":1,"));
    edit(f,"undo");
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"audioGain\":1,"));
    edit(f,"undo"); /* A single gesture undo, then the clip insertion. */
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"clips\":[]"));
    jfx_desktop_frontend_destroy(f);
}
