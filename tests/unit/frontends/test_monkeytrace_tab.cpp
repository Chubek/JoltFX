#include "jfx/desktop_frontend.h"
#include "jfx/jfx_scheduler.h"
#include "jfx_test_backend.h"
#include "tilly/memory.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "joltscript/video_io.h"
#include <cstdio>
#include <cassert>
#include <cstring>
#include <cmath>
#include <cfloat>

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
 jfx_scene3d_render_options_t options={}; options.size=sizeof(options);
 assert(jfx_desktop_frontend_scene3d_render_options(nullptr,&options)==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_desktop_frontend_scene3d_render_options(f,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_desktop_frontend_set_scene3d_render_options(f,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_desktop_frontend_set_scene3d_render_options(nullptr,&options)==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_desktop_frontend_scene3d_render_options(f,&options)==JFX_SUCCESS);
 bool available=jfx_scene3d_renderer_available(JFX_SCENE3D_RENDER_MONKEYTRACE);
 assert(options.renderer==(available?JFX_SCENE3D_RENDER_MONKEYTRACE:JFX_SCENE3D_RENDER_RASTER));
 auto saved=options;
 options.renderer=JFX_SCENE3D_RENDER_MONKEYTRACE; options.samples=65;
 assert(jfx_desktop_frontend_set_scene3d_render_options(f,&options)==JFX_ERROR_INVALID_ARGUMENT);
 assert(jfx_desktop_frontend_scene3d_render_options(f,&options)==JFX_SUCCESS && options.samples==saved.samples);
 tilly::vector<uint8_t> a(64*48*4),b(a.size());
 assert(jfx_desktop_frontend_render_rgba8(f,64,48,a.data(),a.size())==JFX_SUCCESS);
 draw(f); draw(f);
 if (available) {
  click(f,"Reflections","3D viewport");
  assert(jfx_desktop_frontend_scene3d_render_options(f,&options)==JFX_SUCCESS && options.bounces!=saved.bounces);
  click(f,"Rendering engine","3D viewport"); click(f,"Raster viewport","##Combo_");
  assert(jfx_desktop_frontend_scene3d_render_options(f,&options)==JFX_SUCCESS && options.renderer==JFX_SCENE3D_RENDER_RASTER);
  assert(jfx_desktop_frontend_render_rgba8(f,64,48,b.data(),b.size())==JFX_SUCCESS && a!=b);
  assert(jfx_desktop_frontend_set_scene3d_render_options(f,&saved)==JFX_SUCCESS);
  draw(f); // A worker owns a snapshot while edits/seek affect the live document.
  assert(jfx_desktop_frontend_edit(f,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
  assert(jfx_desktop_frontend_edit(f,"3d.key",0,0,30,3,"")==JFX_SUCCESS);
  assert(jfx_desktop_frontend_seek(f,1)==JFX_SUCCESS);
  assert(jfx_desktop_frontend_render_rgba8(f,64,48,b.data(),b.size())==JFX_SUCCESS && a!=b);
  scheduler_wait_idle(); draw(f); scheduler_wait_idle(); draw(f);
  if (argc>1 && jfx_export_available() && jfx_export_codec_available("ffv1",false)) {
   jfx_export_options_t video={}; video.size=sizeof(video); video.path=argv[1]; video.video_codec="ffv1";
   video.width=64; video.height=48; video.start_frame=30; video.frame_count=1;
   jfx_export_job_t *job=nullptr;
   assert(jfx_desktop_frontend_export_begin(f,&video,&job)==JFX_SUCCESS);
   assert(jfx_export_set_scene3d_render_options(nullptr,&saved)==JFX_ERROR_INVALID_ARGUMENT);
   assert(jfx_export_set_scene3d_render_options(job,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
   // Export captured both the scene and quality before these UI mutations.
   assert(jfx_desktop_frontend_edit(f,"3d.color",0,0,0,.95,"")==JFX_SUCCESS);
   options.renderer=JFX_SCENE3D_RENDER_RASTER;
   assert(jfx_desktop_frontend_set_scene3d_render_options(f,&options)==JFX_SUCCESS);
   assert(jfx_export_step(job,1)==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_COMPLETE);
   assert(jfx_export_set_scene3d_render_options(job,&saved)==JFX_ERROR_BUSY);
   assert(jolt_video_io_frame(argv[1],0,64,48,a.data(),a.size())==0);
   assert(a==b); jfx_export_destroy(job); std::remove(argv[1]);
  }
 } else {
  options.renderer=JFX_SCENE3D_RENDER_MONKEYTRACE;
  assert(jfx_desktop_frontend_set_scene3d_render_options(f,&options)==JFX_ERROR_NOT_IMPLEMENTED);
 }
 draw(f); jfx_desktop_frontend_destroy(f); // outstanding snapshot jobs drain safely
}
