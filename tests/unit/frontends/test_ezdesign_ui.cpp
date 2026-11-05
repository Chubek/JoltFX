#include "jfx/desktop_frontend.h"
#include "jfx_test_backend.h"
#include "../../../frontends/desktop/src/ezdesign_workspace.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../../third_party/ImGuizmo/src/ImGuizmo.h"
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
using namespace jfx::ez;
static void frame(jfx_desktop_frontend_t *f){assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS);}
static void move(jfx_desktop_frontend_t *f,ImVec2 p){ImGui::GetIO().AddMousePosEvent(p.x,p.y);frame(f);}
static void click(jfx_desktop_frontend_t *f,ImVec2 p){move(f,p);ImGui::GetIO().AddMouseButtonEvent(0,true);frame(f);ImGui::GetIO().AddMouseButtonEvent(0,false);frame(f);frame(f);}
static void key(jfx_desktop_frontend_t *f,ImGuiKey k,bool ctrl=false,bool shift=false){auto &io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);io.AddKeyEvent(ImGuiMod_Shift,shift);io.AddKeyEvent(k,true);frame(f);io.AddKeyEvent(k,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);frame(f);}
static ImGuiWindow *window(const char *part){for(auto *w:ImGui::GetCurrentContext()->Windows)if(w->Active && std::strstr(w->Name,part))return w;assert(false);return nullptr;}
static ImVec2 locate(jfx_desktop_frontend_t *f,const char *part,const char *label,int index=-1,const char *parent=nullptr){
    move(f,{0,0});auto *w=window(part);ImGui::DebugLocateItem(parent?ImHashStr(label,0,w->GetID(parent)):index<0?w->GetID(label):ImHashStr(label,0,w->GetID(index)));frame(f);
    ImRect bounds=w->InnerRect;bounds.Expand(4);ImVec2 low(FLT_MAX,FLT_MAX),high(-FLT_MAX,-FLT_MAX);
    for(auto &v:ImGui::GetForegroundDrawList()->VtxBuffer)if(bounds.Contains(v.pos)){low.x=std::min(low.x,v.pos.x);low.y=std::min(low.y,v.pos.y);high.x=std::max(high.x,v.pos.x);high.y=std::max(high.y,v.pos.y);}
    assert(low.x<=high.x);return {(low.x+high.x)*.5f,(low.y+high.y)*.5f};
}
static ImVec2 point(jfx_desktop_frontend_t *f,Vec p){
    auto *w=window("/EZDesign viewport_");glm::dvec2 pixel;assert(workspace(f)->tools.camera.project(p,pixel));
    return {w->InnerRect.Min.x+ImGui::GetStyle().WindowPadding.x+float(pixel.x),w->InnerRect.Min.y+ImGui::GetStyle().WindowPadding.y+float(pixel.y)};
}
static void exact(jfx_desktop_frontend_t *f,const char *value){
    click(f,locate(f,"/EZDesign tools_","##EZ measurement"));key(f,ImGuiKey_A,true);ImGui::GetIO().AddInputCharactersUTF8(value);frame(f);key(f,ImGuiKey_Enter);
}
int main(){
    jfx_desktop_frontend_config_t config{};config.size=sizeof(config);config.width=1440;config.height=1000;config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr;assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_EZDESIGN)==JFX_SUCCESS);frame(f);frame(f);frame(f);
    auto &e=workspace(f)->tools;assert(e.editor.doc.scopes[0].faces.empty());
    click(f,locate(f,"/EZDesign tools_","Top"));assert(e.camera.view.orthographic);
    click(f,locate(f,"/EZDesign tools_","Rectangle (R)"));click(f,point(f,{-2,0,1.5}));move(f,point(f,{2,0,-1.5}));
    assert(e.preview_valid);auto preview=e.preview_mesh.triangles.size();assert(e.editor.doc.scopes[0].faces.empty());click(f,point(f,{2,0,-1.5}));
    assert(e.editor.doc.scopes[0].faces.size()==1);assert(e.index.mesh.triangles.size()==preview);
    click(f,locate(f,"JoltFX workspace","Undo"));assert(e.editor.doc.scopes[0].faces.empty());
    click(f,locate(f,"JoltFX workspace","Redo"));assert(e.editor.doc.scopes[0].faces.size()==1);
    // Cancelled preview and tab switches preserve committed geometry/history.
    key(f,ImGuiKey_R);click(f,point(f,{-1,0,1}));move(f,point(f,{1,0,-1}));key(f,ImGuiKey_Escape);assert(e.editor.doc.scopes[0].faces.size()==1);
    key(f,ImGuiKey_Z,true);assert(e.editor.doc.scopes[0].faces.empty());key(f,ImGuiKey_Z,true,true);assert(e.editor.doc.scopes[0].faces.size()==1);
    key(f,ImGuiKey_T);click(f,point(f,{-2,0,1.5}));click(f,point(f,{2,0,1.5}));assert(e.editor.doc.dimensions.size()==1);
    // Real tool click followed by real numeric-entry widget commits one extrusion.
    key(f,ImGuiKey_P);click(f,point(f,{0,0,0}));move(f,point(f,{0,0,-.8}));assert(e.started);exact(f,"300 mm");
    assert(!e.started);assert(e.editor.doc.scopes[0].faces.size()==6);validate_solid(e.editor.doc.scopes[0]);
    click(f,point(f,{0,.3,0}));key(f,ImGuiKey_Escape); // clear input ownership and cancel an accidental new pull
    key(f,ImGuiKey_O);click(f,point(f,{-.8,.3,.6}));move(f,point(f,{.8,.3,-.6}));assert(e.preview_valid);
    preview=e.preview_mesh.triangles.size();click(f,point(f,{.8,.3,-.6}));assert(e.editor.doc.scopes[0].faces.size()==10);assert(e.index.mesh.triangles.size()==preview);
    key(f,ImGuiKey_V);click(f,point(f,{1.4,.3,0}));assert(e.selected.kind==Kind::Face);
    Id painted=e.selected.id;click(f,locate(f,"/EZDesign inspector_","Paint face",1));assert(face(e.editor.doc.scopes[0],painted).front==1);
    // Holed face handle edits shared shell vertices; release commits once,
    // Escape and workspace changes discard the candidate.
    auto face_before=e.editor.doc;auto face_revision=e.editor.revision;
    auto drag_face=[&](int cancel){
        auto ids=entity_vertices(e.editor.doc.scopes[0],painted);Vec center(0);
        for(Id id:ids)center+=vertex(e.editor.doc.scopes[0],id).position;
        ImVec2 handle=point(f,center/double(ids.size()));handle.x+=25;
        move(f,handle);ImGui::GetIO().AddMouseButtonEvent(0,true);frame(f);assert(ImGuizmo::IsUsing());
        for(int i=1;i<=4;++i)move(f,{handle.x+float(i)*10,handle.y});
        assert(e.preview_valid);assert(e.editor.revision==face_revision);
        if(cancel==1)key(f,ImGuiKey_Escape);
        if(cancel==2){assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS);frame(f);}
        ImGui::GetIO().AddMouseButtonEvent(0,false);frame(f);frame(f);
        if(cancel==2){assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_EZDESIGN)==JFX_SUCCESS);frame(f);frame(f);}
    };
    drag_face(0);assert(e.editor.revision==face_revision+1);validate_solid(e.editor.doc.scopes[0]);
    Id corner=face(face_before.scopes[0],painted).outer[0];
    assert(vertex(e.editor.doc.scopes[0],corner).position!=vertex(face_before.scopes[0],corner).position);
    key(f,ImGuiKey_Z,true);assert(vertex(e.editor.doc.scopes[0],corner).position==vertex(face_before.scopes[0],corner).position);
    face_revision=e.editor.revision;drag_face(1);drag_face(2);assert(e.editor.revision==face_revision);
    // Palette drafts do not mutate the document; explicit Apply is one undo step.
    click(f,locate(f,"/EZDesign inspector_","Materials"));
    click(f,locate(f,"/EZDesign inspector_","Create material"));assert(e.editor.doc.materials.size()==4);
    auto original_color=e.editor.doc.materials[3].color;auto palette_revision=e.editor.revision;
    ImVec2 red=locate(f,"/EZDesign inspector_","##X",-1,"Color");
    move(f,red);ImGui::GetIO().AddMouseButtonEvent(0,true);frame(f);
    move(f,{red.x+35,red.y});ImGui::GetIO().AddMouseButtonEvent(0,false);frame(f);
    assert(e.editor.doc.materials[3].color==original_color && e.editor.revision==palette_revision);
    click(f,locate(f,"/EZDesign inspector_","Apply color"));
    Vec edited_color=e.editor.doc.materials[3].color;assert(edited_color!=original_color);
    key(f,ImGuiKey_Z,true);assert(e.editor.doc.materials[3].color==original_color);
    key(f,ImGuiKey_Z,true,true);assert(e.editor.doc.materials[3].color==edited_color);
    click(f,locate(f,"/EZDesign inspector_","Materials"));
    click(f,locate(f,"/EZDesign inspector_","Paint back side"));
    click(f,locate(f,"/EZDesign inspector_","Paint face",3));
    assert(face(e.editor.doc.scopes[0],painted).front==1 && face(e.editor.doc.scopes[0],painted).back==3);
    click(f,locate(f,"/EZDesign inspector_","Reverse face"));
    assert(face(e.editor.doc.scopes[0],painted).front==3 && face(e.editor.doc.scopes[0],painted).back==1);
    key(f,ImGuiKey_Z,true);assert(face(e.editor.doc.scopes[0],painted).back==3);validate_solid(e.editor.doc.scopes[0]);
    // Turn the model into a shared definition and place a linked instance.
    click(f,locate(f,"/EZDesign inspector_","Component from context"));assert(e.editor.doc.scopes[0].instances.size()==1);
    key(f,ImGuiKey_V);click(f,point(f,{1.4,.3,0}));assert(e.selected.kind==Kind::Component);
    click(f,locate(f,"/EZDesign inspector_","Linked copy"));assert(e.editor.doc.scopes[0].instances.size()==2);
    Id definition=e.editor.doc.scopes[0].instances[0].definition;assert(e.editor.doc.scopes[0].instances[1].definition==definition);
    // Activate the actual ImGuizmo translation handle. One release is one undo
    // step; cancellation does not modify either instance transform.
    auto drag=[&](bool cancel){ImVec2 handle=point(f,{0,0,0});handle.x+=25;
        move(f,handle);ImGui::GetIO().AddMouseButtonEvent(0,true);frame(f);assert(ImGuizmo::IsUsing());
        for(int i=1;i<=4;++i)move(f,{handle.x+float(i)*10,handle.y});
        if(cancel)key(f,ImGuiKey_Escape);
        ImGui::GetIO().AddMouseButtonEvent(0,false);frame(f);frame(f);
    };
    auto before_transform=e.editor.doc.scopes[0].instances[0].transform;drag(false);
    assert(e.editor.doc.scopes[0].instances[0].transform!=before_transform);key(f,ImGuiKey_Z,true);assert(e.editor.doc.scopes[0].instances[0].transform==before_transform);
    drag(true);assert(e.editor.doc.scopes[0].instances[0].transform==before_transform);
    // Native File API dispatches the same payload as the panel's save action.
    assert(jfx_desktop_frontend_save_project(f,"ezdesign-ui-test.jfx")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_close_project(f)==JFX_SUCCESS);assert(e.editor.doc.scopes[0].instances.empty());
    assert(jfx_desktop_frontend_open_project(f,"ezdesign-ui-test.jfx")==JFX_SUCCESS);frame(f);frame(f);
    assert(jfx_desktop_frontend_workspace(f)==JFX_DESKTOP_WORKSPACE_EZDESIGN);assert(e.editor.doc.scopes[0].instances.size()==2);assert(scope(e.editor.doc,definition).faces.size()==10);
    assert(e.editor.doc.dimensions.size()==1 && face(scope(e.editor.doc,definition),painted).front==1);
    assert(face(scope(e.editor.doc,definition),painted).back==3);
    assert(e.editor.doc.materials.size()==4 && e.editor.doc.materials[3].color==edited_color);
    assert(workspace(f)->load(e.path));assert(std::strcmp(e.path,"ezdesign-ui-test.jfx")==0);frame(f);
    assert(jfx_desktop_frontend_save_project(f,"ezdesign-wrong-extension.txt")==JFX_ERROR_INVALID_ARGUMENT);
    // Camera text focus prevents wheel/navigation/shortcut leakage.
    auto before=e.camera.view;click(f,locate(f,"/EZDesign tools_","##EZ measurement"));key(f,ImGuiKey_R);assert(e.tool==Tool::Select);assert(e.camera.view.distance==before.distance);
    key(f,ImGuiKey_Escape);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS);frame(f);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_EZDESIGN)==JFX_SUCCESS);frame(f);frame(f);assert(!e.started);
    jfx_desktop_frontend_destroy(f);std::remove("ezdesign-ui-test.jfx");
}
