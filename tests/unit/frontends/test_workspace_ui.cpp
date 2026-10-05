/* Drive actual ImGui tabs/wheels/dials and verify rendered documents/history. */
#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdio>

static void frame(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static void move(jfx_desktop_frontend_t *f,ImVec2 p) { ImGui::GetIO().AddMousePosEvent(p.x,p.y); frame(f); }
static void button(jfx_desktop_frontend_t *f,bool down) { ImGui::GetIO().AddMouseButtonEvent(0,down); frame(f); }
static void edit(jfx_desktop_frontend_t *f,const char *op) { assert(jfx_desktop_frontend_edit(f,op,0,0,0,0,"")==JFX_SUCCESS); }
static void render(jfx_desktop_frontend_t *f,uint8_t out[4]) { assert(jfx_desktop_frontend_render_rgba8(f,1,1,out,4)==JFX_SUCCESS); }
static ImGuiTabBar *tabs() {
    ImGuiWindow *window=ImGui::FindWindowByName("JoltFX workspace"); assert(window);
    auto *bar=ImGui::GetCurrentContext()->TabBars.GetByKey(window->GetID("Interfaces")); assert(bar);
    return bar;
}
static void selected(jfx_desktop_frontend_t *f,jfx_desktop_workspace_t workspace,const char *name) {
    frame(f); frame(f);
    assert(jfx_desktop_frontend_workspace(f)==workspace);
    ImGuiTabItem *tab=ImGui::TabBarFindTabByID(tabs(),tabs()->SelectedTabId); assert(tab);
    assert(!std::strcmp(ImGui::TabBarGetTabName(tabs(),tab),name));
}
static void click_tab(jfx_desktop_frontend_t *f,const char *name) {
    auto *bar=tabs(); ImVec2 center; bool found=false;
    for (auto &tab:bar->Tabs) if (!std::strcmp(ImGui::TabBarGetTabName(bar,&tab),name)) {
        center=ImVec2(bar->BarRect.Min.x+tab.Offset+tab.Width*.5f-bar->ScrollingAnim,bar->BarRect.GetCenter().y);
        found=true; break;
    }
    assert(found); move(f,center); button(f,true); button(f,false); frame(f);
}
static ImVec2 first_dial() {
    float x=std::numeric_limits<float>::max(),y=x;
    auto *data=ImGui::GetDrawData();
    for (int l=0;l<data->CmdListsCount;++l) for (const auto &v:data->CmdLists[l]->VtxBuffer)
        if (v.col==IM_COL32(40,48,62,255)) y=std::fmin(y,v.pos.y);
    for (int l=0;l<data->CmdListsCount;++l) for (const auto &v:data->CmdLists[l]->VtxBuffer)
        if (v.col==IM_COL32(40,48,62,255) && v.pos.y<y+52) x=std::fmin(x,v.pos.x);
    assert(x<std::numeric_limits<float>::max());
    float radius=26*ImGui::GetFontSize()/13.0f;
    return ImVec2(x+radius,y+radius);
}
static ImVec2 first_wheel() {
    auto *data=ImGui::GetDrawData();
    for (int l=0;l<data->CmdListsCount;++l) for (const auto &v:data->CmdLists[l]->VtxBuffer)
        if (v.col==IM_COL32(70,73,80,255)) return v.pos;
    assert(false); return ImVec2();
}
static void undo_shortcut(jfx_desktop_frontend_t *f) {
    ImGuiIO &io=ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_Z,true); frame(f);
    io.AddKeyEvent(ImGuiKey_Z,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame(f);
}
static ImVec2 locate(jfx_desktop_frontend_t *f,ImGuiWindow *window,const char *label) {
    move(f,ImVec2(0,0));
    ImGui::DebugLocateItem(window->GetID(label)); frame(f);
    auto *draw=ImGui::GetForegroundDrawList();
    /* Locate draws a widget outline and a line from the mouse. Only outline
     * vertices inside this channel matter; the pointer starts outside it. */
    ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    for (const auto &v:draw->VtxBuffer) if (window->InnerRect.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x); lo.y=std::fmin(lo.y,v.pos.y);
        hi.x=std::fmax(hi.x,v.pos.x); hi.y=std::fmax(hi.y,v.pos.y);
    }
    assert(lo.x<=hi.x && lo.y<=hi.y);
    return ImVec2((lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f);
}
static void wave(const char *path) {
    FILE *file=std::fopen(path,"wb"); assert(file);
    auto le=[&](unsigned v,unsigned bytes) { for (unsigned i=0;i<bytes;++i) std::fputc((int)((v>>(i*8))&255),file); };
    std::fwrite("RIFF",1,4,file); le(36+16000,4); std::fwrite("WAVEfmt ",1,8,file);
    le(16,4); le(1,2); le(1,2); le(8000,4); le(16000,4); le(2,2); le(16,2);
    std::fwrite("data",1,4,file); le(16000,4);
    for (int i=0;i<8000;++i) le(8192,2);
    assert(!std::fclose(file));
}
static float mix_peak(jfx_desktop_frontend_t *f) {
    jfx_audio_mixer_t *mixer=nullptr;
    assert(jfx_desktop_frontend_audio_mixer(f,8000,&mixer)==JFX_SUCCESS);
    float pcm[128]; assert(jfx_audio_mixer_render(mixer,200,64,pcm,128)==JFX_SUCCESS);
    jfx_audio_mixer_destroy(mixer); return pcm[0];
}
int main(int argc,char **argv) {
    assert(argc==2);
    jfx_desktop_frontend_config_t config{}; config.size=sizeof(config); config.width=1280; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    /* A small source raster keeps gesture checks about interaction, not CPU rendering throughput. */
    assert(jfx_desktop_frontend_edit(f,"sequence.new",4,2,30,1,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"clip.add",0,0,0,30,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(nullptr,JFX_DESKTOP_WORKSPACE_NLE)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_COUNT)==JFX_ERROR_INVALID_ARGUMENT);
    const char *names[]={"NLE","Layer Effects","Color Calibration","Color Grading","Node Compositing","Plugins","Console","Statistics","Audio Mixing"};
    /* Exercise both later and earlier requested tabs while all tabs are visible. */
    for (int i=JFX_DESKTOP_WORKSPACE_COUNT-1;i>=0;--i) {
        assert(jfx_desktop_frontend_set_workspace(f,(jfx_desktop_workspace_t)i)==JFX_SUCCESS);
        selected(f,(jfx_desktop_workspace_t)i,names[i]);
    }
    assert(jfx_desktop_frontend_set_panel_visible(f,JFX_DESKTOP_PANEL_VIEWPORT,false)==JFX_SUCCESS);
    /* Audio Mixing selects the sequence even when entered from a graph. Its
     * real widgets edit the shared document and participate in history. */
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_COMPOSITING)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_AUDIO)==JFX_SUCCESS);
    selected(f,JFX_DESKTOP_WORKSPACE_AUDIO,"Audio Mixing");
    ImGuiWindow *channel=nullptr;
    for (auto *w:ImGui::GetCurrentContext()->Windows)
        if (std::strstr(w->Name,"/Audio channel_")) { channel=w; break; }
    assert(channel);
    ImGui::GetCurrentContext()->NavNextActivateId=channel->GetID("Mute");
    frame(f); frame(f);
    char audio_state[4096];
    assert(jfx_desktop_frontend_sequence_state(f,audio_state,sizeof(audio_state))==JFX_SUCCESS);
    assert(std::strstr(audio_state,"\"muted\":true"));
    edit(f,"undo");
    assert(jfx_desktop_frontend_sequence_state(f,audio_state,sizeof(audio_state))==JFX_SUCCESS);
    assert(std::strstr(audio_state,"\"muted\":false"));
    assert(jfx_desktop_frontend_set_panel_visible(f,JFX_DESKTOP_PANEL_AUDIO,false)==JFX_SUCCESS);
    assert(!jfx_desktop_frontend_panel_visible(f,JFX_DESKTOP_PANEL_AUDIO));
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_AUDIO)==JFX_SUCCESS);
    selected(f,JFX_DESKTOP_WORKSPACE_AUDIO,"Audio Mixing");
    wave("workspace-audio-ui.wav");
    assert(jfx_desktop_frontend_edit(f,"sequence.new",4,2,30,1,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"track.add",0,0,0,0,"Music")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"clip.add",0,JFX_CLIP_AUDIO,0,30,"workspace-audio-ui.wav")==JFX_SUCCESS);
    frame(f); frame(f); assert(mix_peak(f)==.25f);
    ImVec2 fader=locate(f,channel,"##Track gain");
    move(f,fader); button(f,true);
    for (int i=1;i<=4;++i) move(f,ImVec2(fader.x,fader.y-6*(float)i));
    button(f,false);
    float mixed=mix_peak(f); assert(mixed!=.25f);
    edit(f,"undo"); assert(mix_peak(f)==.25f);
    edit(f,"redo"); assert(mix_peak(f)==mixed);
    edit(f,"undo"); edit(f,"undo"); /* One drag undo, then the clip insertion. */
    assert(mix_peak(f)==0);
    edit(f,"redo");
    frame(f); frame(f);
    ImGuiWindow *inspector=nullptr;
    for (auto *w:ImGui::GetCurrentContext()->Windows)
        if (w->Active && std::strstr(w->Name,"/Editor interface_")) { inspector=w; break; }
    assert(inspector);
    ImVec2 balance=locate(f,inspector,"Stereo balance");
    move(f,balance); button(f,true); move(f,ImVec2(balance.x+40,balance.y)); button(f,false);
    float balanced=mix_peak(f);
    std::fprintf(stderr,"balance widget %.1f %.1f; mixed %.6f; expected id %u active %u\n",(double)balance.x,(double)balance.y,(double)balanced,inspector->GetID("Stereo balance"),ImGui::GetCurrentContext()->ActiveId);
    assert(balanced<.25f);
    edit(f,"undo"); assert(mix_peak(f)==.25f);
    edit(f,"redo"); assert(mix_peak(f)==balanced);
    assert(jfx_desktop_frontend_save_project(f,"workspace-audio-ui.jfx")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(f,"workspace-audio-ui.jfx")==JFX_SUCCESS);
    assert(mix_peak(f)==balanced);
    assert(!std::remove("workspace-audio-ui.jfx"));
    assert(!std::remove("workspace-audio-ui.wav"));
    assert(jfx_desktop_frontend_edit(f,"sequence.new",4,2,30,1,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"track.add",0,0,0,0,"Video")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"clip.add",0,0,0,30,"")==JFX_SUCCESS);
    frame(f); frame(f);
    click_tab(f,"Color Grading"); selected(f,JFX_DESKTOP_WORKSPACE_GRADING,"Color Grading");
    assert(jfx_desktop_frontend_layer_effects_add(f,0,0,"grade_primary")==JFX_SUCCESS); frame(f); frame(f);
    uint8_t before[4],after[4],restored[4]; render(f,before);
    ImVec2 dial=first_dial(); move(f,dial); button(f,true);
    for (int i=1;i<=4;++i) move(f,ImVec2(dial.x+5*(float)i,dial.y-2*(float)i));
    render(f,after); assert(std::memcmp(before,after,4)); /* Live preview during a gesture. */
    button(f,false);
    undo_shortcut(f); render(f,restored); assert(!std::memcmp(before,restored,4));
    edit(f,"redo"); render(f,restored); assert(!std::memcmp(after,restored,4));
    edit(f,"undo"); edit(f,"undo"); /* The second undo removes the operator, not another drag sample. */
    char state[4096]; assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS);
    assert(std::strstr(state,"\"effects\":0"));

    assert(jfx_desktop_frontend_layer_effects_add(f,0,0,"grade_color_wheels")==JFX_SUCCESS); frame(f); frame(f);
    render(f,before); ImVec2 wheel=first_wheel(); move(f,wheel); button(f,true);
    for (int i=1;i<=4;++i) move(f,ImVec2(wheel.x+5*(float)i,wheel.y-2*(float)i));
    render(f,after); assert(std::memcmp(before,after,4));
    /* Switching interfaces while dragging must commit the complete RGB gesture. */
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_EFFECTS)==JFX_SUCCESS);
    button(f,false); selected(f,JFX_DESKTOP_WORKSPACE_EFFECTS,"Layer Effects");
    edit(f,"undo"); render(f,restored); assert(!std::memcmp(before,restored,4));
    edit(f,"redo"); render(f,restored); assert(!std::memcmp(after,restored,4));
    edit(f,"undo"); edit(f,"undo");
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"effects\":0"));
    /* Plugin actions participate in the same history and preview as the editor tabs. */
    uint32_t id=0;
    assert(!jfx_desktop_frontend_plugins(nullptr));
    assert(jfx_desktop_frontend_load_plugin(nullptr,argv[1],&id)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_unload_plugin(nullptr,1)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_invoke_plugin(nullptr,"missing")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_load_plugin(f,argv[1],&id)==JFX_SUCCESS);
    click_tab(f,"Plugins"); selected(f,JFX_DESKTOP_WORKSPACE_PLUGINS,"Plugins");
    render(f,before); assert(jfx_desktop_frontend_invoke_plugin(f,"org.joltfx.example.apply")==JFX_SUCCESS);
    render(f,after); assert(after[0]>before[0] && after[2]<before[2]);
    assert(jfx_desktop_frontend_unload_plugin(f,id)==JFX_ERROR_BUSY);
    edit(f,"undo"); render(f,restored); assert(!std::memcmp(before,restored,4));
    assert(jfx_desktop_frontend_unload_plugin(f,id)==JFX_ERROR_BUSY);
    assert(jfx_desktop_frontend_close_project(f)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_unload_plugin(f,id)==JFX_SUCCESS);
    /* Hidden tabs and narrow, high-DPI layouts still compose without invalid geometry. */
    assert(jfx_desktop_frontend_set_panel_visible(f,JFX_DESKTOP_PANEL_COLOR_GRADING,false)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_GRADING)==JFX_SUCCESS);
    selected(f,JFX_DESKTOP_WORKSPACE_GRADING,"Color Grading");
    assert(jfx_desktop_frontend_edit(f,"track.add",0,0,0,0,"Video")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"clip.add",0,0,0,30,"solid")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_layer_effects_add(f,0,0,"lift_gamma_gain")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_resize(f,600,900)==JFX_SUCCESS); ImGui::GetIO().FontGlobalScale=1.5f;
    frame(f); frame(f);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_AUDIO)==JFX_SUCCESS);
    frame(f); frame(f);
    auto *data=ImGui::GetDrawData();
    for (int l=0;l<data->CmdListsCount;++l) for (const auto &v:data->CmdLists[l]->VtxBuffer) assert(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));
    jfx_desktop_frontend_destroy(f);
}
