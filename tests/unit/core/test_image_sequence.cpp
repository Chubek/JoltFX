#include "jfx/jfx_export.h"
#include "jfx/jfx_image.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>

static tilly::string frame_path(const char *prefix,unsigned frame) {
    char suffix[32]; std::snprintf(suffix,sizeof(suffix),"_%06u.png",frame);
    return tilly::string(prefix)+suffix;
}
static void check_image(const char *path,const tilly::vector<uint8_t> &expected) {
    jfx_image_t image{}; image.size=sizeof(image);
    assert(jfx_image_load(path,nullptr,&image)==JFX_SUCCESS);
    assert(image.width==96 && image.height==64);
    assert(!std::memcmp(image.pixels,expected.data(),expected.size()));
    jfx_image_release(&image);
}
static void sequence(const char *prefix,jfx_scene3d_renderer_t renderer) {
    auto *editor=jfx_editor_create(96,64); assert(editor);
    assert(jfx_editor_command(editor,"3d.new",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.clock",2,4,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.key",0,0,3,3,"")==JFX_SUCCESS);
    auto *snapshot=jfx_scene3d_clone(jfx_editor_scene3d(editor)); assert(snapshot);
    jfx_export_options_t options{}; options.size=sizeof(options);
    options.path=prefix; options.container="png_sequence";
    options.width=96; options.height=64; options.start_frame=1; options.frame_count=2;
    jfx_export_job_t *job=nullptr;
    assert(jfx_export_begin(editor,&options,&job)==JFX_SUCCESS);
    jfx_scene3d_render_options_t render{}; render.size=sizeof(render); render.renderer=renderer;
    assert(jfx_export_set_scene3d_render_options(job,&render)==JFX_SUCCESS);
    assert(jfx_export_total_frames(job)==2 && jfx_export_completed_frames(job)==0);
    // Later scene, rate, path and renderer settings cannot alter the job.
    assert(jfx_editor_command(editor,"3d.color",0,0,0,.99,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.clock",60,100,0,0,"")==JFX_SUCCESS);
    options.path="not-the-output"; render.renderer=JFX_SCENE3D_RENDER_VULKAN;
    tilly::vector<uint8_t> first(96*64*4),second(first.size());
    render.renderer=renderer;
    assert(jfx_scene3d_render_ex(snapshot,.5,96,64,&render,first.data(),first.size())==JFX_SUCCESS);
    assert(jfx_scene3d_render_ex(snapshot,1,96,64,&render,second.data(),second.size())==JFX_SUCCESS);
    assert(first!=second);
    assert(jfx_export_step(job,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_export_step(job,1)==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_RUNNING);
    assert(jfx_export_completed_frames(job)==1);
    check_image(frame_path(prefix,1).c_str(),first);
    assert(!std::filesystem::exists(frame_path(prefix,0).c_str()));
    assert(!std::filesystem::exists(frame_path(prefix,2).c_str()));
    assert(jfx_export_set_scene3d_render_options(job,&render)==JFX_ERROR_BUSY);
    assert(jfx_export_step(job,1)==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_COMPLETE);
    check_image(frame_path(prefix,2).c_str(),second);
    jfx_export_cancel(job); assert(jfx_export_state(job)==JFX_EXPORT_COMPLETE);
    jfx_export_destroy(job);
    std::remove(frame_path(prefix,1).c_str()); std::remove(frame_path(prefix,2).c_str());

    options.path=prefix; options.start_frame=0; options.frame_count=3;
    assert(jfx_export_begin(editor,&options,&job)==JFX_SUCCESS);
    assert(jfx_export_set_scene3d_render_options(job,&render)==JFX_SUCCESS);
    assert(jfx_export_step(job,1)==JFX_SUCCESS);
    jfx_export_cancel(job); assert(jfx_export_state(job)==JFX_EXPORT_CANCELLED);
    assert(jfx_export_completed_frames(job)==1);
    assert(jfx_export_step(job,1)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_export_destroy(job);
    assert(std::filesystem::exists(frame_path(prefix,0).c_str()));
    assert(!std::filesystem::exists(frame_path(prefix,1).c_str()));
    std::remove(frame_path(prefix,0).c_str());
    jfx_scene3d_destroy(snapshot); jfx_editor_destroy(editor);
}
int main(int argc,char **argv) {
    assert(argc==2);
    sequence(argv[1],JFX_SCENE3D_RENDER_RASTER);
    if (jfx_scene3d_renderer_available(JFX_SCENE3D_RENDER_FILAMENT)) sequence(argv[1],JFX_SCENE3D_RENDER_FILAMENT);
    auto *e=jfx_editor_create(96,64); assert(e);
    jfx_export_options_t o{}; o.size=sizeof(o); o.path=argv[1]; o.container="png_sequence"; o.width=96; o.height=64;
    jfx_export_job_t *j=nullptr;
    assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_INVALID_ARGUMENT); // scene-only
    assert(jfx_editor_command(e,"3d.new",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"3d.clock",24,3,0,0,"")==JFX_SUCCESS);
    o.start_frame=3; assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_INVALID_ARGUMENT && !j);
    o.start_frame=1; o.frame_count=3; assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_INVALID_ARGUMENT && !j);
    o.frame_count=0; o.width=2049; assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_INVALID_ARGUMENT && !j);
    o.width=96; o.audio=true; assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_INVALID_ARGUMENT && !j);
    o.audio=false; o.fps_num=30; assert(jfx_export_begin(e,&o,&j)==JFX_ERROR_INVALID_ARGUMENT && !j);
    o.fps_num=0;
    tilly::string missing=tilly::string(argv[1])+"-missing/frame"; o.path=missing.c_str();
    assert(jfx_export_begin(e,&o,&j)==JFX_SUCCESS && jfx_export_total_frames(j)==2);
    assert(jfx_export_step(j,1)!=JFX_SUCCESS && jfx_export_state(j)==JFX_EXPORT_FAILED);
    assert(jfx_export_completed_frames(j)==0);
    assert(jfx_export_step(j,1)!=JFX_SUCCESS);
    jfx_export_destroy(j);
    // A failed publication keeps the completed frame and existing destination,
    // and removes its exclusive temporary sibling. Percent tokens are literal.
    tilly::string literal=tilly::string(argv[1])+"-%06d";
    o.path=literal.c_str();
    auto first=frame_path(o.path,1),blocked=frame_path(o.path,2);
    assert(std::filesystem::create_directory(blocked.c_str()));
    auto marker=blocked+"/keep";
    FILE *file=std::fopen(marker.c_str(),"wb"); assert(file);
    assert(std::fputs("existing output",file)>=0); assert(std::fclose(file)==0);
    assert(jfx_export_begin(e,&o,&j)==JFX_SUCCESS);
    assert(jfx_export_step(j,2)!=JFX_SUCCESS && jfx_export_state(j)==JFX_EXPORT_FAILED);
    assert(jfx_export_completed_frames(j)==1 && std::filesystem::exists(first.c_str()));
    assert(std::filesystem::exists(marker.c_str()));
    assert(!std::filesystem::exists((blocked+".jfx-part-0").c_str()));
    jfx_export_destroy(j);
    std::remove(first.c_str()); std::remove(marker.c_str());
    assert(std::filesystem::remove(blocked.c_str()));
    jfx_editor_destroy(e);
}
