#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "../../../frontends/desktop/src/workspace_history.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <string>

static void frame(jfx_desktop_frontend_t *f){assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS);}
static void move(jfx_desktop_frontend_t *f,ImVec2 p){ImGui::GetIO().AddMousePosEvent(p.x,p.y);frame(f);}
static ImGuiWindow *window(const char *part){for(auto *w:ImGui::GetCurrentContext()->Windows)if(w->Active && std::strstr(w->Name,part))return w;assert(false);return nullptr;}
static ImVec2 locate(jfx_desktop_frontend_t *f,const char *part,const char *label,const char *parent=nullptr){
    move(f,{0,0});auto *w=window(part);ImGui::DebugLocateItem(parent?ImHashStr(label,0,w->GetID(parent)):w->GetID(label));frame(f);
    ImVec2 low(FLT_MAX,FLT_MAX),high(-FLT_MAX,-FLT_MAX);auto bounds=w->Rect();bounds.Expand(4);
    for(auto &v:ImGui::GetForegroundDrawList()->VtxBuffer)if(bounds.Contains(v.pos)){low.x=std::min(low.x,v.pos.x);low.y=std::min(low.y,v.pos.y);high.x=std::max(high.x,v.pos.x);high.y=std::max(high.y,v.pos.y);}
    if(low.x>high.x)std::fprintf(stderr,"Could not locate %s in %s\n",label,part);
    assert(low.x<=high.x);return {(low.x+high.x)*.5f,(low.y+high.y)*.5f};
}
static void click(jfx_desktop_frontend_t *f,ImVec2 p){move(f,p);ImGui::GetIO().AddMouseButtonEvent(0,true);frame(f);ImGui::GetIO().AddMouseButtonEvent(0,false);frame(f);frame(f);}
static void button(jfx_desktop_frontend_t *f,const char *label){click(f,locate(f,"JoltFX workspace",label));}
static void key(jfx_desktop_frontend_t *f,ImGuiKey k,bool shift=false){auto &io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiMod_Shift,shift);io.AddKeyEvent(k,true);frame(f);io.AddKeyEvent(k,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);frame(f);}
static void select(jfx_desktop_frontend_t *f,jfx_desktop_workspace_t workspace){assert(jfx_desktop_frontend_set_workspace(f,workspace)==JFX_SUCCESS);frame(f);frame(f);}
static std::string source(jfx_desktop_frontend_t *f){char text[8192];assert(jfx_desktop_frontend_creative_source(f,text,sizeof(text))==JFX_SUCCESS);return text;}
static std::string state(jfx_desktop_frontend_t *f,int kind){
    char text[65536];auto r=kind==2?jfx_desktop_frontend_scene3d_state(f,text,sizeof(text)):kind==1?jfx_desktop_frontend_graph_state(f,text,sizeof(text)):jfx_desktop_frontend_sequence_state(f,text,sizeof(text));assert(r==JFX_SUCCESS);
    std::string out=text;
    for(const char *field:{"\"canUndo\":","\"canRedo\":","\"undo\":","\"redo\":","\"active\":"}){
        auto at=out.find(field);if(at!=std::string::npos)out.erase(at,out.find(',',at)-at+1);
    }return out;
}
static std::string game_source(jfx_desktop_frontend_t *f){
    const char *path="workspace-history.ing";assert(jfx_desktop_frontend_save_project(f,path)==JFX_SUCCESS);
    FILE *file=std::fopen(path,"rb");assert(file);char text[16384];size_t n=std::fread(text,1,sizeof(text)-1,file);text[n]=0;std::fclose(file);std::remove(path);return text;
}
int main(){
    // Bounded history drops oldest entries, ignores no-ops and invalidates redo
    // on a branch; independent documents never share their stacks.
    jfx_history::History<int> history;
    for(int i=1;i<=40;++i)history.record(i);
    for(int i=39;i>=8;--i){assert(history.step(false));assert(history.current==i);}
    assert(!history.step(false));assert(history.step(true));history.record(9);assert(history.redo_count==31);
    history.record(100);assert(!history.step(true));assert(history.step(false) && history.current==9);

    jfx_desktop_frontend_config_t config{};config.size=sizeof(config);config.width=1440;config.height=1000;config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr;assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);ImGui::GetIO().IniFilename=nullptr;
    frame(f);frame(f);
    // Every shared-document tab exposes the same working top-level controls.
    for(auto tab:{JFX_DESKTOP_WORKSPACE_NLE,JFX_DESKTOP_WORKSPACE_EFFECTS,JFX_DESKTOP_WORKSPACE_CALIBRATION,JFX_DESKTOP_WORKSPACE_GRADING,JFX_DESKTOP_WORKSPACE_COMPOSITING,JFX_DESKTOP_WORKSPACE_AUDIO,JFX_DESKTOP_WORKSPACE_DAW,JFX_DESKTOP_WORKSPACE_CONSOLE,JFX_DESKTOP_WORKSPACE_STATISTICS,JFX_DESKTOP_WORKSPACE_PLUGINS,JFX_DESKTOP_WORKSPACE_MODELING3D,JFX_DESKTOP_WORKSPACE_GAME}){
        select(f,tab);int kind=tab==JFX_DESKTOP_WORKSPACE_COMPOSITING?1:tab==JFX_DESKTOP_WORKSPACE_MODELING3D || tab==JFX_DESKTOP_WORKSPACE_GAME?2:0;
        const char *op=kind==2?"3d.add":kind==1?"node.add":"track.add";
        auto before=state(f,kind);assert(jfx_desktop_frontend_edit(f,op,0,0,0,0,kind==2?"cube":kind==1?"exposure":"History track")==JFX_SUCCESS);
        auto after=state(f,kind);assert(before!=after);frame(f);button(f,"Undo");
        if(state(f,kind)!=before)std::fprintf(stderr,"Undo mismatch in tab %d: before %s after %s\n",int(tab),before.c_str(),state(f,kind).c_str());
        assert(state(f,kind)==before);button(f,"Redo");assert(state(f,kind)==after);
    }
    auto scene=state(f,2);auto sequence=state(f,0);
    select(f,JFX_DESKTOP_WORKSPACE_CREATIVE);auto initial=source(f);
    const char *a="(defkernel a [x y t] x)",*b="(defkernel b [x y t] y)";
    assert(jfx_desktop_frontend_creative_set_source(f,a)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_creative_set_source(f,b)==JFX_SUCCESS);frame(f);
    button(f,"Undo");assert(source(f)==a);key(f,ImGuiKey_Y);assert(source(f)==b);
    click(f,locate(f,"##MainMenuBar","Edit","##MenuBar"));
    click(f,locate(f,"##Menu_00","Undo"));assert(source(f)==a);
    click(f,locate(f,"##MainMenuBar","Edit","##MenuBar"));
    click(f,locate(f,"##Menu_00","Redo"));assert(source(f)==b);
    key(f,ImGuiKey_Z);assert(source(f)==a);key(f,ImGuiKey_Z);assert(source(f)==initial);
    key(f,ImGuiKey_Z,true);assert(source(f)==a);
    // Branching after undo removes the abandoned redo state.
    assert(jfx_desktop_frontend_creative_set_source(f,"(defkernel c [x] x)")==JFX_SUCCESS);
    auto branch=source(f);button(f,"Redo");assert(source(f)==branch);
    // Text-widget undo must not traverse workspace history while typing.
    click(f,locate(f,"/Creative code_","##Creative source"));key(f,ImGuiKey_A);
    ImGui::GetIO().AddInputCharactersUTF8(b);frame(f);assert(source(f)==b);
    key(f,ImGuiKey_Z);assert(source(f)!=a && source(f)!=initial);
    for(int i=0;i<64 && source(f)!=branch;++i)key(f,ImGuiKey_Z);
    assert(source(f)==branch);
    // Finish the edit with an explicit API change and verify persistence across tabs.
    assert(jfx_desktop_frontend_creative_set_source(f,b)==JFX_SUCCESS);
    select(f,JFX_DESKTOP_WORKSPACE_NLE);select(f,JFX_DESKTOP_WORKSPACE_CREATIVE);
    button(f,"Undo");assert(source(f)==branch);button(f,"Redo");assert(source(f)==b);
    assert(state(f,2)==scene && state(f,0)==sequence);
    // GameDev scene and source histories stay independent, including optional-off builds.
    select(f,JFX_DESKTOP_WORKSPACE_GAME);
    assert(jfx_desktop_frontend_game_set_source(f,"fn first() { return 1; }")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_game_set_source(f,"fn second() { return 2; }")==JFX_SUCCESS);frame(f);
    button(f,"Undo source");assert(game_source(f)=="fn first() { return 1; }");
    button(f,"Redo source");assert(game_source(f)=="fn second() { return 2; }");assert(state(f,2)==scene);
    click(f,locate(f,"/Editor interface_","Ingagi","GameDev modes"));frame(f);
    button(f,"Undo");assert(game_source(f)=="fn first() { return 1; }");
    key(f,ImGuiKey_Z,true);assert(game_source(f)=="fn second() { return 2; }");assert(state(f,2)==scene);
    jfx_desktop_frontend_destroy(f);
}
