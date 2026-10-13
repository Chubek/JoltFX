#include "jfx/desktop_frontend.h"
#include "jfx/jfx_scheduler.h"
#include "jfx_test_backend.h"
#include "joltscript/video_io.h"
#include "tilly/memory.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

static void draw(jfx_desktop_frontend_t *f) { assert(jfx_desktop_frontend_draw(f)==JFX_SUCCESS); }
static ImGuiWindow *window(const char *part) {
    for (auto *w:ImGui::GetCurrentContext()->Windows) if (w->Active && std::strstr(w->Name,part)) return w;
    assert(false); return nullptr;
}
static void click(jfx_desktop_frontend_t *f,const char *label,const char *part) {
    auto *w=window(part); auto &io=ImGui::GetIO(); io.AddMousePosEvent(0,0); draw(f);
    ImGui::DebugLocateItem(w->GetID(label)); draw(f);
    ImRect bounds=w->InnerRect; bounds.Expand(4); ImVec2 lo(FLT_MAX,FLT_MAX),hi(-FLT_MAX,-FLT_MAX);
    for (auto &v:ImGui::GetForegroundDrawList()->VtxBuffer) if (bounds.Contains(v.pos)) {
        lo.x=std::fmin(lo.x,v.pos.x); lo.y=std::fmin(lo.y,v.pos.y); hi.x=std::fmax(hi.x,v.pos.x); hi.y=std::fmax(hi.y,v.pos.y);
    }
    assert(lo.x<=hi.x); io.AddMousePosEvent((lo.x+hi.x)/2,(lo.y+hi.y)/2); draw(f);
    io.AddMouseButtonEvent(0,true); draw(f); io.AddMouseButtonEvent(0,false); draw(f); draw(f);
}
int main(int argc,char **argv) {
    jfx_desktop_frontend_config_t config={}; config.size=sizeof(config); config.width=1440; config.height=1000; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *f=nullptr; assert(jfx_desktop_frontend_create(&config,&f)==JFX_SUCCESS);
    ImGui::GetIO().IniFilename=nullptr;
    assert(jfx_desktop_frontend_set_workspace(f,JFX_DESKTOP_WORKSPACE_MODELING3D)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    jfx_scene3d_render_options_t options={}; options.size=sizeof(options); options.renderer=JFX_SCENE3D_RENDER_FILAMENT;
    if (!jfx_scene3d_renderer_available(options.renderer)) {
        assert(jfx_desktop_frontend_set_scene3d_render_options(f,&options)==JFX_ERROR_NOT_IMPLEMENTED);
        jfx_desktop_frontend_destroy(f); return 0;
    }
    draw(f); draw(f);
    click(f,"Rendering engine","3D viewport"); click(f,"Filament (GPU rasterizer)","##Combo_");
    assert(jfx_desktop_frontend_scene3d_render_options(f,&options)==JFX_SUCCESS && options.renderer==JFX_SCENE3D_RENDER_FILAMENT);
    tilly::vector<uint8_t> a(96*64*4),b(a.size());
    assert(jfx_desktop_frontend_render_rgba8(f,96,64,a.data(),a.size())==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f,"3d.key",0,0,30,3,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_seek(f,1)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_render_rgba8(f,96,64,b.data(),b.size())==JFX_SUCCESS && a!=b);
    scheduler_wait_idle(); draw(f); scheduler_wait_idle(); draw(f);
    if (argc>1 && jfx_export_available() && jfx_export_codec_available("ffv1",false)) {
        jfx_export_options_t video={}; video.size=sizeof(video); video.path=argv[1]; video.video_codec="ffv1";
        video.width=96; video.height=64; video.start_frame=30; video.frame_count=1;
        jfx_export_job_t *job=nullptr;
        assert(jfx_desktop_frontend_export_begin(f,&video,&job)==JFX_SUCCESS);
        assert(jfx_desktop_frontend_edit(f,"3d.color",0,0,0,.95,"")==JFX_SUCCESS);
        options.renderer=JFX_SCENE3D_RENDER_RASTER;
        assert(jfx_desktop_frontend_set_scene3d_render_options(f,&options)==JFX_SUCCESS);
        assert(jfx_export_step(job,1)==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_COMPLETE);
        assert(jolt_video_io_frame(argv[1],0,96,64,a.data(),a.size())==0 && a==b);
        jfx_export_destroy(job); std::remove(argv[1]);
    }
    jfx_desktop_frontend_destroy(f);
}
