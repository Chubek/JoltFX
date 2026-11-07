#include "jfx/desktop_frontend.h"
#include "jfx/jfx_scheduler.h"
#include "jfx_test_backend.h"
#include "page_design_workspace.h"
#include "preferences.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

static void frame(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f) == JFX_SUCCESS); }
static void move(jfx_desktop_frontend_t *f, ImVec2 p) { ImGui::GetIO().AddMousePosEvent(p.x,p.y); frame(f); }
static void click(jfx_desktop_frontend_t *f, ImVec2 p) {
    move(f,p); ImGui::GetIO().AddMouseButtonEvent(0,true); frame(f);
    ImGui::GetIO().AddMouseButtonEvent(0,false); frame(f); frame(f);
}
static ImGuiWindow *window(const char *part) {
    for (auto *w : ImGui::GetCurrentContext()->Windows) if (w->Active && std::strstr(w->Name,part)) return w;
    assert(false); return nullptr;
}
static ImVec2 locate(jfx_desktop_frontend_t *f, const char *part, const char *label) {
    move(f,{0,0}); auto *w=window(part);
    auto id=!std::strcmp(label,"IDE Mode")?ImHashStr(label,0,w->GetID("Page Design modes")):w->GetID(label);
    ImGui::DebugLocateItem(id); frame(f);
    ImRect bounds=w->InnerRect; bounds.Expand(4); ImVec2 lo(FLT_MAX,FLT_MAX), hi(-FLT_MAX,-FLT_MAX);
    for (auto &v : ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x=std::min(lo.x,v.pos.x); lo.y=std::min(lo.y,v.pos.y); hi.x=std::max(hi.x,v.pos.x); hi.y=std::max(hi.y,v.pos.y);
    }
    if (lo.x>hi.x) std::fprintf(stderr,"Missing widget %s in %s\n",label,part);
    assert(lo.x<=hi.x); return {(lo.x+hi.x)*.5f,(lo.y+hi.y)*.5f};
}
static void key(jfx_desktop_frontend_t *f, ImGuiKey k, bool ctrl=false) {
    auto &io=ImGui::GetIO(); io.AddKeyEvent(ImGuiMod_Ctrl,ctrl); io.AddKeyEvent(k,true); frame(f);
    io.AddKeyEvent(k,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); frame(f);
}
static void replace(jfx_desktop_frontend_t *f, const char *part, const char *label, const char *value) {
    click(f,locate(f,part,label)); key(f,ImGuiKey_A,true); ImGui::GetIO().AddInputCharactersUTF8(value); frame(f);
}
int main() {
    // A persisted zero interval must disable automatic publication at startup.
    jfx_preferences::Paths paths; assert(jfx_preferences::resolve_paths(paths));
    auto prefs=jfx_preferences::defaults(); prefs.page_auto_commit_seconds=0; char error[256];
    assert(jfx_preferences::save(paths.config,prefs,error,sizeof(error)));
    jfx_desktop_frontend_config_t config{}; config.size=sizeof(config); config.width=1440; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_PAGE_DESIGN)==JFX_SUCCESS);
    frame(f); frame(f); frame(f); auto *w=jfx::page::workspace(f);
    const char *editor="/Editor interface_";
    auto original=w->session.source();
    const auto first=w->session.committed().elements[0];
    auto *canvas=window("/Page preview_");
    const float fit=w->preview_scale();
    assert(std::abs(float(w->session.preview().pages[0].width)*fit-canvas->InnerRect.GetWidth())<25);
    ImVec2 start(canvas->DC.CursorStartPos.x+float(first.x+15)*fit,canvas->DC.CursorStartPos.y+float(first.y+10)*fit);
    move(f,start); ImGui::GetIO().AddMouseButtonEvent(0,true); frame(f);
    move(f,{start.x+30,start.y+20});
    assert(w->session.source()==original && !w->dirty());
    ImGui::GetIO().AddMouseButtonEvent(0,false); frame(f); frame(f);
    assert(w->dirty() && w->session.source()==original);
    assert(std::abs(w->session.preview().elements[0].x-first.x-30.0/double(fit))<.1);
    auto moved=w->session.preview().elements[0].x;
    click(f,locate(f,editor,"Commit"));
    assert(!w->dirty() && w->session.committed().elements[0].x==moved);
    click(f,locate(f,"JoltFX workspace","Undo")); assert(w->session.source()==original);
    click(f,locate(f,"JoltFX workspace","Redo")); assert(w->session.committed().elements[0].x==moved);
    // Escape and a workspace switch cancel live drags without changing drafts.
    auto before_cancel=w->session.source();
    const auto &moved_frame=w->session.preview().elements[0];
    start={canvas->DC.CursorStartPos.x+float(moved_frame.x+15)*w->preview_scale(),canvas->DC.CursorStartPos.y+float(moved_frame.y+10)*w->preview_scale()};
    for (int cancel=0;cancel<2;++cancel) {
        move(f,start); ImGui::GetIO().AddMouseButtonEvent(0,true); frame(f);
        move(f,{start.x-20,start.y+10});
        if (cancel==0) key(f,ImGuiKey_Escape);
        else { assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS); frame(f); }
        ImGui::GetIO().AddMouseButtonEvent(0,false); frame(f);
        if (cancel==1) { assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_PAGE_DESIGN)==JFX_SUCCESS); frame(f); frame(f); }
        assert(!w->dirty() && w->session.source()==before_cancel);
    }
    // Real inspector text edits remain unpublished until Commit.
    replace(f,"/JoltType editor_","##Frame text","Committed by JoltType");
    assert(w->dirty() && w->session.committed().elements[0].text=="Page Design");
    click(f,locate(f,editor,"Commit"));
    assert(!w->dirty() && w->session.committed().elements[0].text=="Committed by JoltType");
    auto good=w->session.source();
    click(f,locate(f,"/JoltType editor_","Hatch into JoltType"));
    replace(f,"/JoltType editor_","##JoltType source","\\jolttype{1}\n\\page{bad}{297mm}");
    click(f,locate(f,editor,"Commit"));
    assert(w->dirty() && w->session.source()==good && w->session.error().line==2);
    click(f,locate(f,editor,"Revert draft")); assert(!w->dirty()); frame(f);
    assert(w->session.draft()==good); // no stale ImGui source resurrection
    replace(f,"/JoltType editor_","##JoltType source","invalid hatch");
    click(f,locate(f,"/JoltType editor_","Restore hatch"));
    assert(w->session.draft()==good && !w->dirty());
    click(f,locate(f,editor,"Add rectangle")); assert(w->dirty());
    double now=ImGui::GetTime(); ImGui::GetCurrentContext()->Time+=120; frame(f);
    assert(w->dirty() && w->session.source()==good); // global disabled preference
    // A deadline survives workspace changes.
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS); frame(f);
    w->tick(now+121,1,false); assert(!w->dirty());
    assert(w->session.committed().elements.size()==4);
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_PAGE_DESIGN)==JFX_SUCCESS); frame(f); frame(f);
    assert(jfx_desktop_frontend_save_project(f,"page-ui.jtype")==JFX_SUCCESS);
    auto saved=w->session.source();
    assert(jfx_desktop_frontend_close_project(f)==JFX_SUCCESS); assert(w->dirty());
    click(f,locate(f,editor,"Commit")); assert(w->session.committed().elements.empty());
    assert(jfx_desktop_frontend_open_project(f,"page-ui.jtype")==JFX_SUCCESS); frame(f); frame(f);
    assert(w->session.source()==saved && jfx_desktop_frontend_workspace(f)==JFX_DESKTOP_WORKSPACE_PAGE_DESIGN);
    assert(std::strcmp(jfx_desktop_frontend_project_path(f),"page-ui.jtype")==0);
    click(f,locate(f,editor,"Files..."));
    replace(f,"Page Design files","SVG path","page-ui.svg");
    click(f,locate(f,"Page Design files","Export page SVG")); scheduler_wait_idle(); frame(f);
    FILE *svg=std::fopen("page-ui.svg","rb"); assert(svg); char bytes[8192]{};
    assert(std::fread(bytes,1,sizeof(bytes)-1,svg)>0); std::fclose(svg);
    assert(std::strstr(bytes,"<svg") && std::strstr(bytes,"Committed by JoltType"));
    if(jolttype::pdf_available()) {
        replace(f,"Page Design files","Output path","page-ui.pdf");
        click(f,locate(f,"Page Design files","Export document"));scheduler_wait_idle();frame(f);
        FILE *pdf=std::fopen("page-ui.pdf","rb");assert(pdf);
        tilly::string data;size_t n=0;while((n=std::fread(bytes,1,sizeof(bytes),pdf)))data.append(bytes,n);
        std::fclose(pdf);assert(data.find("JoltType / libharu")!=data.npos);
    }
    click(f,locate(f,"Page Design files","Close"));
    click(f,locate(f,editor,"Show Progress Display"));
    scheduler_wait_idle();frame(f);scheduler_wait_idle();frame(f);
    if(jolttype::pdf_available())assert(w->pdf_bytes().find("JoltType / PoDoFo")!=w->pdf_bytes().npos);
    if(jolttype::pdf_preview_available())assert(!w->pdf_bitmap().rgba.empty());
    assert(window("PDF progress pane"));
    click(f,locate(f,editor,"Hide Progress Display"));
    click(f,locate(f,editor,"IDE Mode"));frame(f);
    click(f,locate(f,"/JoltType editor_","Package tools"));
    replace(f,"Typelet package","Package name","UI package");
    click(f,locate(f,"Typelet package","Apply metadata"));
    assert(w->project.manifest.name=="UI package");
    replace(f,"Typelet package","Typelet path","page-ui.typelet");
    click(f,locate(f,"Typelet package","Build + Pack Typelet"));scheduler_wait_idle();frame(f);
    FILE *package=std::fopen("page-ui.typelet","rb");assert(package);std::fclose(package);
    click(f,locate(f,"Typelet package","Close package tools"));
    // IDE gives the page a third of the workspace and keeps draft compilation
    // independent of publication, visibility and the auto-update switch.
    canvas=window("/Page preview_");auto *code=window("/JoltType editor_");
    assert(std::abs(code->Size.x/canvas->Size.x-2.f)<.15f);
    const char *inline_source="\\jolttype{1}\\page{600}{800}\\inline{title}{20}{20}{\\component{content}{Live inline}\\text{label}{0}{0}{250}{20}{#123456}{@content}}";
    replace(f,"/JoltType editor_","##JoltType source",inline_source);
    ImGui::GetCurrentContext()->Time+=.3;frame(f);
    assert(w->session.preview().entities.size()==1 && w->session.preview().elements[0].text=="Live inline");
    assert(w->session.source()==saved && w->dirty());
    click(f,locate(f,editor,"Hide page preview"));frame(f);
    for(auto *item:ImGui::GetCurrentContext()->Windows)assert(!item->Active || !std::strstr(item->Name,"/Page preview_"));
    click(f,locate(f,editor,"Auto-update preview"));
    replace(f,"/JoltType editor_","##JoltType source","\\jolttype{1}\\page{600}{800}\\text{manual}{10}{10}{200}{12}{#000000}{Manual}");
    ImGui::GetCurrentContext()->Time+=1;frame(f);
    assert(w->session.preview().elements[0].text=="Live inline");
    click(f,locate(f,editor,"Update preview"));assert(w->session.preview().elements[0].text=="Manual");
    click(f,locate(f,editor,"Auto-update preview"));
    replace(f,"/JoltType editor_","##JoltType source",inline_source);
    ImGui::GetCurrentContext()->Time+=.3;frame(f);
    assert(w->session.preview().elements[0].text=="Live inline"); // hidden updates continue
    replace(f,"/JoltType editor_","##JoltType source","\\jolttype{1}\\inline{");
    ImGui::GetCurrentContext()->Time+=.3;frame(f);
    assert(w->session.error().message[0] && w->session.preview().elements[0].text=="Live inline");
    click(f,locate(f,editor,"Show page preview"));
    click(f,locate(f,editor,"Revert draft"));
    // Insert a real module-backed Pagelet, edit its data and capture a Kliche.
    click(f,locate(f,"/JoltType editor_","Insert Pagelet"));
    assert(w->session.preview().entities.size()==1 && w->project.inventory.find("design-elements"));
    assert(w->session.preview().elements.back().text=="New title");
    click(f,locate(f,"/JoltType editor_","Component / configuration"));
    click(f,locate(f,"##Combo_00","content"));
    replace(f,"/JoltType editor_","##Component value","Private instance contents");
    click(f,locate(f,"/JoltType editor_","Apply component"));
    assert(w->session.preview().elements.back().text=="Private instance contents");
    click(f,locate(f,"/JoltType editor_","Create Kliche"));
    const auto *style=w->project.inventory.find("MyStyle");
    assert(style && style->kind==jolttype::AssetKind::Kliche);
    assert(style->bytes.find("Private instance contents")==style->bytes.npos);
    click(f,locate(f,"/JoltType editor_","Insert Pagelet"));
    assert(w->session.preview().entities.size()==2 && w->session.preview().elements.back().text=="New title");
    click(f,locate(f,editor,"Commit"));
    assert(w->session.committed().entities.size()==2);
    click(f,locate(f,"/JoltType editor_","Pagelet / Kliche"));
    click(f,locate(f,"##Combo_00","DoubleRuleTable"));
    click(f,locate(f,"/JoltType editor_","Insert Pagelet"));
    assert(w->session.preview().entities.size()==3);
    const auto table=w->session.preview().entities.back();
    assert(table.type=="DoubleRuleTable" && table.width==300);
    auto child_begin=w->session.preview().elements.size()-4;
    assert(w->session.preview().elements[child_begin+2].y-w->session.preview().elements[child_begin+1].y==4);
    canvas=window("/Page preview_");
    start={canvas->DC.CursorStartPos.x+30*w->preview_scale(),canvas->DC.CursorStartPos.y+65*w->preview_scale()};
    move(f,start);ImGui::GetIO().AddMouseButtonEvent(0,true);frame(f);
    move(f,{start.x+15,start.y+10});ImGui::GetIO().AddMouseButtonEvent(0,false);frame(f);frame(f);
    assert(w->session.preview().entities.back().x>table.x);
    const auto &moved_table=w->session.preview().entities.back();
    assert(std::abs(w->session.preview().elements[child_begin].x-moved_table.x-6)<.001);
    assert(std::abs(w->session.preview().elements[child_begin+1].x-moved_table.x)<.001);
    click(f,locate(f,editor,"Commit"));
    // Search the shipped catalog and insert one of the 30 additional modules.
    click(f,locate(f,"/JoltType editor_","Pagelet / Kliche"));
    replace(f,"##Combo_00","##Pagelet filter","contact");
    click(f,locate(f,"##Combo_00","ContactCard"));
    click(f,locate(f,editor,"Insert Pagelet"));
    assert(w->session.preview().entities.back().type=="ContactCard");
    const auto *contact=w->project.inventory.find("contact-card");
    assert(contact && contact->kind==jolttype::AssetKind::Pagelet);
    // Reusing a bundled library must preserve its document-owned source edits.
    auto customized=*contact;auto at=customized.bytes.find("Alex Morgan");assert(at!=customized.bytes.npos);
    customized.bytes.replace(at,std::strlen("Alex Morgan"),"Custom default");
    jolttype::Diagnostic design_error;assert(w->project.inventory.put(customized,design_error));
    assert(w->session.set_inventory(w->project.inventory,design_error));
    assert(w->session.stage("\\jolttype{1}\\page{210mm}{297mm}",ImGui::GetTime()) && w->session.refresh());
    frame(f);click(f,locate(f,editor,"Insert Pagelet"));
    assert(w->session.preview().entities.size()==1 && w->session.preview().entities[0].type=="ContactCard");
    assert(w->project.inventory.find("contact-card")->bytes==customized.bytes);
    bool custom_name=false;for(const auto &e:w->session.preview().elements)if(e.text=="Custom default")custom_name=true;
    assert(custom_name);click(f,locate(f,editor,"Commit"));
    jfx_desktop_frontend_destroy(f);
    // Persisted interval is used by the real draw loop, even off-tab.
    prefs.page_auto_commit_seconds=1;
    assert(jfx_preferences::save(paths.config,prefs,error,sizeof(error)));
    assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_open_project(f,"page-ui.jtype")==JFX_SUCCESS); frame(f); frame(f);
    w=jfx::page::workspace(f);
    click(f,locate(f,editor,"Add page")); assert(w->dirty());
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_NLE)==JFX_SUCCESS); frame(f);
    ImGui::GetCurrentContext()->Time+=2; frame(f);
    assert(!w->dirty() && w->session.committed().pages.size()==2);
    jfx_desktop_frontend_destroy(f);
    std::remove("page-ui.jtype"); std::remove("page-ui.svg"); std::remove("page-ui.pdf");std::remove("page-ui.typelet");std::remove(paths.config);
}
