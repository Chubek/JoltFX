#include "jfx/desktop_frontend.h"
#include "jfx/jfx_image.h"
#include "jfx/jfx_scheduler.h"
#include "jfx_test_backend.h"
#include "tilly/memory.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static ImGuiWindow *window(const char *part) {
    for (auto *w:ImGui::GetCurrentContext()->Windows) if (w->Active && std::strstr(w->Name,part)) return w;
    assert(false); return nullptr;
}
static void click(jfx_desktop_frontend_t *f,const char *label,const char *part="3D inspector",bool tab=false) {
    auto *w=window(part); auto &io=ImGui::GetIO(); io.AddMousePosEvent(0,0); draw(f);
    ImGui::DebugLocateItem(tab?ImHashStr(label,0,w->GetID("3D modes")):w->GetID(label)); draw(f);
    ImRect bounds=w->InnerRect; bounds.Expand(4); ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    for (auto &v:ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x); lo.y=std::fmin(lo.y,v.pos.y); hi.x=std::fmax(hi.x,v.pos.x); hi.y=std::fmax(hi.y,v.pos.y);
    }
    if (lo.x>hi.x) std::fprintf(stderr,"Missing widget: %s in %s\n",label,w->Name);
    assert(lo.x<=hi.x); io.AddMousePosEvent((lo.x+hi.x)/2,(lo.y+hi.y)/2); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f); io.AddMouseButtonEvent(0,false); draw(f); draw(f);
}
static void text(jfx_desktop_frontend_t *f,const char *label,const char *value) {
    click(f,label); auto &io=ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl,true); io.AddKeyEvent(ImGuiKey_A,true); draw(f);
    io.AddKeyEvent(ImGuiKey_A,false); io.AddKeyEvent(ImGuiMod_Ctrl,false); draw(f);
    io.AddInputCharactersUTF8(value); draw(f);
    io.AddKeyEvent(ImGuiKey_Enter,true); draw(f); io.AddKeyEvent(ImGuiKey_Enter,false); draw(f);
}
static tilly::string path(const char *prefix,unsigned frame) {
    char suffix[32]; std::snprintf(suffix,sizeof(suffix),"_%06u.png",frame); return tilly::string(prefix)+suffix;
}
int main(int argc,char **argv) {
    assert(argc==2);
    jfx_desktop_frontend_config_t config{}; config.size=sizeof(config); config.width=1440; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_MODELING3D)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.clock",2,4,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.key",0,0,3,3,"")==JFX_SUCCESS);
    jfx_scene3d_render_options_t renderer{}; renderer.size=sizeof(renderer); renderer.renderer=JFX_SCENE3D_RENDER_RASTER;
    assert(jfx_desktop_frontend_set_scene3d_render_options(f,&renderer)==JFX_SUCCESS);
    draw(f); draw(f); click(f,"Rendering","/Editor interface_",true);
    assert(jfx_desktop_frontend_scene3d_render_options(f,&renderer)==JFX_SUCCESS);
    assert(renderer.renderer==(jfx_scene3d_renderer_available(JFX_SCENE3D_RENDER_FILAMENT)?JFX_SCENE3D_RENDER_FILAMENT:JFX_SCENE3D_RENDER_RASTER));
    text(f,"Sequence prefix",argv[1]); text(f,"Render width","96"); text(f,"Render height","64");
    text(f,"First frame","1"); text(f,"Frame count (0: remaining)","2");
    tilly::vector<uint8_t> expected[2]={tilly::vector<uint8_t>(96*64*4),tilly::vector<uint8_t>(96*64*4)};
    for (unsigned i=0;i<2;++i) {
        assert(jfx_desktop_frontend_seek(f,double(i+1)/2)==JFX_SUCCESS);
        assert(jfx_desktop_frontend_render_rgba8(f,96,64,expected[i].data(),expected[i].size())==JFX_SUCCESS);
    }
    click(f,"Render PNG sequence");
    assert(jfx_desktop_frontend_edit(f,"3d.color",0,0,0,.99,"")==JFX_SUCCESS);
    renderer.renderer=JFX_SCENE3D_RENDER_RASTER;
    assert(jfx_desktop_frontend_set_scene3d_render_options(f,&renderer)==JFX_SUCCESS);
    click(f,"Animation","/Editor interface_",true); // export continues outside Rendering
    for (int i=0;i<8;++i) { scheduler_wait_idle(); draw(f); }
    assert(!std::filesystem::exists(path(argv[1],0).c_str()));
    for (unsigned i=0;i<2;++i) {
        auto filename=path(argv[1],i+1);
        jfx_image_t image{}; image.size=sizeof(image);
        assert(jfx_image_load(filename.c_str(),nullptr,&image)==JFX_SUCCESS);
        assert(image.width==96 && image.height==64 && !std::memcmp(image.pixels,expected[i].data(),expected[i].size()));
        jfx_image_release(&image); std::remove(filename.c_str());
    }
    click(f,"Rendering","/Editor interface_",true);
    assert(jfx_desktop_frontend_scene3d_render_options(f,&renderer)==JFX_SUCCESS && renderer.renderer==JFX_SCENE3D_RENDER_RASTER);
    assert(jfx_desktop_frontend_edit(f,"3d.clock",24,100,0,0,"")==JFX_SUCCESS);
    text(f,"First frame","0"); text(f,"Frame count (0: remaining)","100");
    click(f,"Render PNG sequence"); click(f,"Cancel image sequence");
    for (int i=0;i<4;++i) { scheduler_wait_idle(); draw(f); }
    unsigned completed=0;
    for (unsigned i=0;i<100;++i) { auto filename=path(argv[1],i); if (std::filesystem::exists(filename.c_str())) { ++completed; std::remove(filename.c_str()); } }
    assert(completed<100);
    jfx_desktop_frontend_destroy(f);
}
