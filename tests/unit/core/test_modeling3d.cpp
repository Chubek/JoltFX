#include "jfx/jfx_editor.h"
#include "jfx/jfx_modeling3d.h"
#include "jfx/jfx_export.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <fstream>
#include <limits>

static void write_file(const char *path,const char *contents) {
    std::ofstream file(path,std::ios::binary); file<<contents; assert(file.good());
}
static void expect_file(const char *path,const char *contents) {
    std::ifstream file(path,std::ios::binary);
    tilly::string actual((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    assert(actual==contents);
}
int main() {
    auto *s=jfx_scene3d_create(); assert(s);
    assert(jfx_scene3d_command(nullptr,"3d.add",0,0,0,0,"cube")!=JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    float x[9]; assert(jfx_scene3d_sample(s,0,0,x)==JFX_SUCCESS && x[6]==1);
    assert(jfx_scene3d_command(s,"3d.key",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.key",0,0,30,2,"")==JFX_SUCCESS);
    assert(jfx_scene3d_sample(s,0,.5,x)==JFX_SUCCESS && std::fabs(x[0]-1.f)<1e-5f);
    assert(jfx_scene3d_command(s,"3d.transform",0,6,0,0,"")!=JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.interpolation",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_sample(s,0,.25,x)==JFX_SUCCESS && x[0]==0);
    assert(jfx_scene3d_command(s,"3d.interpolation",0,0,0,2,"")==JFX_SUCCESS);
    assert(jfx_scene3d_sample(s,0,.25,x)==JFX_SUCCESS && std::fabs(x[0]-.3125f)<1e-5f);
    assert(jfx_scene3d_command(s,"3d.interpolation",0,0,0,1,"")==JFX_SUCCESS);
    tilly::vector<char> text(8*1024*1024), state(1024*1024); size_t n=0;
    assert(jfx_scene3d_save(s,text.data(),text.size(),&n)==JFX_SUCCESS);
    auto *copy=jfx_scene3d_create(); assert(copy);
    assert(jfx_scene3d_load(copy,text.data(),n,nullptr,0)==JFX_SUCCESS);
    assert(jfx_scene3d_sample(copy,0,.5,x)==JFX_SUCCESS && std::fabs(x[0]-1.f)<1e-5f);
    const char *bad="scene3d 1\nvertex nan 0 0\n";
    assert(jfx_scene3d_load(copy,bad,std::strlen(bad),nullptr,0)!=JFX_SUCCESS);
    assert(jfx_scene3d_sample(copy,0,.5,x)==JFX_SUCCESS && std::fabs(x[0]-1.f)<1e-5f);
    assert(!jfx_scene3d_can_undo(copy));
    size_t unchanged=123;
    assert(jfx_scene3d_save(copy,text.data(),1,&unchanged)!=JFX_SUCCESS && unchanged==123);
    float xyz[3],camera[7]; jfx_object3d_info_t info={}; info.size=sizeof(info);
    assert(jfx_scene3d_object_info(nullptr,0,&info)!=JFX_SUCCESS);
    assert(jfx_scene3d_object_info(copy,0,nullptr)!=JFX_SUCCESS);
    assert(jfx_scene3d_vertex(nullptr,0,0,xyz)!=JFX_SUCCESS);
    assert(jfx_scene3d_vertex(copy,0,UINT32_MAX,xyz)!=JFX_SUCCESS);
    assert(jfx_scene3d_camera(nullptr,camera)!=JFX_SUCCESS);
    assert(jfx_scene3d_state(nullptr,state.data(),state.size())!=JFX_SUCCESS);
    assert(jfx_scene3d_load(nullptr,text.data(),n,nullptr,0)!=JFX_SUCCESS);
    assert(jfx_scene3d_sample(copy,0,std::numeric_limits<double>::quiet_NaN(),x)!=JFX_SUCCESS);
    tilly::vector<uint8_t> pixels(128*128*4,99), second(pixels.size());
    assert(jfx_scene3d_render(copy,0,128,128,pixels.data(),pixels.size())==JFX_SUCCESS);
    assert(jfx_scene3d_render(copy,1,128,128,second.data(),second.size())==JFX_SUCCESS);
    assert(pixels!=second);
    second=pixels;
    assert(jfx_scene3d_render(copy,-1,128,128,pixels.data(),pixels.size())!=JFX_SUCCESS && pixels==second);
    write_file("modeling3d-test.ply.tmp","unrelated temporary file");
    write_file("modeling3d-test.ply.jfx-part-0","occupied export slot");
    assert(jfx_scene3d_command(copy,"3d.subdivide",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(copy,"3d.align",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(copy,"3d.export_ply",0,0,0,0,"modeling3d-test.ply")==JFX_SUCCESS);
    assert(jfx_scene3d_command(copy,"3d.import_ply",0,0,0,0,"modeling3d-test.ply")==JFX_SUCCESS);
    expect_file("modeling3d-test.ply.tmp","unrelated temporary file");
    expect_file("modeling3d-test.ply.jfx-part-0","occupied export slot");
    const char *ply_header="ply\nformat ascii 1.0\nelement vertex 3\nproperty float x\nproperty float y\nproperty float z\nelement face 1\nproperty list uchar uint vertex_indices\nend_header\n";
    tilly::string malformed=ply_header; malformed+="0 0 0\n1 0 0\n0 1 0\n4 0 1 2 0\n";
    write_file("modeling3d-bad.ply",malformed.c_str());
    uint32_t count=jfx_scene3d_object_count(copy);
    assert(jfx_scene3d_command(copy,"3d.import_ply",0,0,0,0,"modeling3d-bad.ply")!=JFX_SUCCESS && jfx_scene3d_object_count(copy)==count);
    malformed=ply_header; malformed+="0 0 0\n1 0 0\n0 1 0\n3 0 1";
    write_file("modeling3d-bad.ply",malformed.c_str());
    assert(jfx_scene3d_command(copy,"3d.import_ply",0,0,0,0,"modeling3d-bad.ply")!=JFX_SUCCESS && jfx_scene3d_object_count(copy)==count);
    assert(jfx_scene3d_command(copy,"3d.mass",0,0,0,1,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(copy,"3d.transform",0,1,0,4,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(copy,"3d.bake",0,0,30,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_sample(copy,0,1,x)==JFX_SUCCESS && x[1]<4);
    write_file("modeling3d-test.png","existing image");
    assert(jfx_scene3d_write_png(copy,-1,64,64,"modeling3d-test.png")!=JFX_SUCCESS);
    expect_file("modeling3d-test.png","existing image");
    write_file("modeling3d-test.png.tmp","unrelated temporary file");
    assert(jfx_scene3d_write_png(copy,0,64,64,"modeling3d-test.png")==JFX_SUCCESS);
    assert(jfx_scene3d_write_png(copy,0,64,64,"modeling3d-test.png")==JFX_SUCCESS);
    expect_file("modeling3d-test.png.tmp","unrelated temporary file");
    assert(jfx_scene3d_state(copy,state.data(),state.size())==JFX_SUCCESS);
    assert(std::strstr(state.data(),"\"objects\""));
    auto *e=jfx_editor_create(128,128); assert(e);
    assert(jfx_editor_begin_edit(e,JFX_PROJECT_KIND_GRAPH)==JFX_SUCCESS);
    assert(jfx_editor_set_kind(e,JFX_PROJECT_KIND_GRAPH)==JFX_SUCCESS);
    assert(jfx_editor_command(e,"3d",0,0,0,0,"")==JFX_ERROR_BUSY);
    assert(jfx_editor_command(e,"3d.add",0,0,0,0,"cube")==JFX_ERROR_BUSY);
    assert(jfx_editor_cancel_edit(e)==JFX_SUCCESS && jfx_editor_kind(e)==JFX_PROJECT_KIND_SEQUENCE);
    assert(jfx_editor_command(e,"3d.add",0,0,0,0,"sphere")==JFX_SUCCESS);
    assert(jfx_editor_kind(e)==JFX_PROJECT_KIND_SCENE3D);
    assert(jfx_editor_command(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_save(e,text.data(),text.size(),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,text.data(),n,nullptr,0)==JFX_SUCCESS);
    assert(jfx_editor_render(e,0,128,128,pixels.data(),pixels.size())==JFX_SUCCESS);
    if (jfx_export_available() && jfx_export_codec_available("ffv1",false)) {
        jfx_export_options_t options={}; options.size=sizeof(options); options.path="modeling3d-test.mkv";
        options.container="matroska"; options.video_codec="ffv1"; options.frame_count=2;
        options.width=4096; options.height=64; options.audio=true;
        jfx_export_job_t *job=nullptr;
        assert(jfx_export_begin(e,&options,&job)==JFX_ERROR_INVALID_ARGUMENT && !job);
        options.width=64; options.fps_num=60;
        assert(jfx_export_begin(e,&options,&job)==JFX_ERROR_INVALID_ARGUMENT && !job);
        options.fps_num=30;
        assert(jfx_export_begin(e,&options,&job)==JFX_SUCCESS && job);
        assert(jfx_editor_command(e,"3d.new",0,0,0,0,"")==JFX_SUCCESS);
        assert(jfx_export_step(job,2)==JFX_SUCCESS && jfx_export_state(job)==JFX_EXPORT_COMPLETE);
        assert(jfx_export_completed_frames(job)==2);
        jfx_export_destroy(job); std::remove("modeling3d-test.mkv");
    }
    const char *nonmanifold="scene3d 1\nclock 30 120\ncamera 35 22 7 0 0 0 45\nobject \"mesh\" 1 0\nv 0 0 0\nv 1 0 0\nv 0 1 0\nv 0 0 1\nv 0 -1 0\nf 0 1 2\nf 1 0 3\nf 0 1 4\nend\n";
    assert(jfx_scene3d_load(s,nonmanifold,std::strlen(nonmanifold),nullptr,0)==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.subdivide",0,0,0,0,"")!=JFX_SUCCESS);
    assert(!jfx_scene3d_can_undo(s));
    assert(jfx_scene3d_command(s,"3d.new",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.add",0,0,0,0,"plane")==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.mass",1,0,0,1,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.transform",1,1,0,4,"")==JFX_SUCCESS);
    assert(jfx_scene3d_command(s,"3d.bake",0,0,90,0,"")==JFX_SUCCESS);
    assert(jfx_scene3d_sample(s,1,3,x)==JFX_SUCCESS && x[1]>.8f && x[1]<1.3f);
    jfx_editor_destroy(e); jfx_scene3d_destroy(copy); jfx_scene3d_destroy(s);
    std::remove("modeling3d-test.ply"); std::remove("modeling3d-test.png");
    std::remove("modeling3d-test.ply.tmp"); std::remove("modeling3d-test.png.tmp");
    std::remove("modeling3d-test.ply.jfx-part-0"); std::remove("modeling3d-bad.ply");
}
