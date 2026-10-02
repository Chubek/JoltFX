#include "jfx/desktop_frontend.h"
#include "jfx/mobile_player.h"
#include "jfx/web_session.h"
#include "jfx_test_backend.h"
#include <cassert>
#include <cstdio>
#include <cstring>

template<class Edit> void edit_sequence(Edit edit) {
    assert(edit("clip.split",0,0,0,6,"")==JFX_SUCCESS);
    assert(edit("clip.move",0,1,1,4,"")==JFX_SUCCESS);
    assert(edit("clip.slip",1,0,0,3,"")==JFX_SUCCESS);
    assert(edit("clip.duplicate",0,1,0,24,"")==JFX_SUCCESS);
    assert(edit("clip.ripple_delete",0,1,0,0,"")==JFX_SUCCESS);
    assert(edit("undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(edit("calibration.add",1,0,0,0,"calib_lut")==JFX_SUCCESS);
    assert(edit("grade.add",1,0,0,0,"grade_primary")==JFX_SUCCESS);
    assert(edit("grade.param",1,0,0,1,"exposure")==JFX_SUCCESS);
    assert(edit("undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(edit("redo",0,0,0,0,"")==JFX_SUCCESS);
}
static void check_export(const char *path) {
    FILE *file=std::fopen(path,"rb"); assert(file); char ppm[64];
    size_t n=std::fread(ppm,1,sizeof(ppm),file); std::fclose(file);
    const unsigned char expected[]={ 'P','6','\n','2',' ','1','\n','2','5','5','\n',204,102,51,204,102,51 };
    assert(n==sizeof(expected) && !std::memcmp(ppm,expected,n));
    assert(!std::remove(path));
}
int main(int argc,char **argv) {
    assert(argc==2);
    const char *path=JOLT_TEST_ROOT "/tests/fixtures/nle_sequence.jfx";
    char doc[4096]; FILE *file=std::fopen(path,"rb"); assert(file);
    size_t n=std::fread(doc,1,sizeof(doc),file); std::fclose(file);
    uint8_t desktop_pixels[8],mobile_pixels[8],web_pixels[8];
    char desktop_state[8192],mobile_state[8192],web_state[8192];
    jfx_desktop_frontend_config_t dc{}; dc.size=sizeof(dc); dc.width=320; dc.height=180; dc.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *desktop=nullptr; assert(jfx_desktop_frontend_create(&dc,&desktop)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(desktop,path)==JFX_SUCCESS);
    edit_sequence([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) { return jfx_desktop_frontend_edit(desktop,op,a,b,c,v,text); });
    assert(jfx_desktop_frontend_timeline_set_current_frame(desktop,5)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_render_rgba8(desktop,2,1,desktop_pixels,8)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_sequence_state(desktop,desktop_state,sizeof(desktop_state))==JFX_SUCCESS);
    assert(desktop_pixels[0]==204 && desktop_pixels[1]==102 && desktop_pixels[3]==255);
    assert(jfx_desktop_frontend_write_frame(desktop,5,argv[1])==JFX_SUCCESS); check_export(argv[1]);
    file=std::fopen(argv[1],"wb"); assert(file);
    assert(std::fwrite("keep",1,4,file)==4); assert(!std::fclose(file));
    assert(jfx_desktop_frontend_edit(desktop,"effect.add",1,0,0,0,"grade_lut")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(desktop,"effect.path",1,0,2,0,"/missing/joltfx-nle-look.cube")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_write_frame(desktop,5,argv[1])!=JFX_SUCCESS);
    file=std::fopen(argv[1],"rb"); assert(file);
    char kept[5]={0}; assert(std::fread(kept,1,5,file)==4 && !std::strcmp(kept,"keep"));
    std::fclose(file); assert(!std::remove(argv[1]));
    jfx_desktop_frontend_destroy(desktop);
    jfx_mobile_player_config_t mc{}; mc.size=sizeof(mc); mc.width=2; mc.height=1; mc.duration_seconds=10; mc.backend_name=jfx_test_backend();
    jfx_mobile_player_t *mobile=nullptr; assert(jfx_mobile_player_create(&mc,&mobile)==JFX_SUCCESS);
    assert(jfx_mobile_player_load_document(mobile,doc,n,nullptr,0)==JFX_SUCCESS);
    edit_sequence([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) { return jfx_mobile_player_edit(mobile,op,a,b,c,v,text); });
    assert(jfx_mobile_player_seek(mobile,5.0/30)==JFX_SUCCESS);
    assert(jfx_mobile_player_render_rgba8(mobile,0,mobile_pixels,8)==JFX_SUCCESS);
    assert(jfx_mobile_player_sequence_state(mobile,mobile_state,sizeof(mobile_state))==JFX_SUCCESS);
    assert(!std::memcmp(desktop_pixels,mobile_pixels,8)); assert(!std::strcmp(desktop_state,mobile_state));
    assert(jfx_mobile_player_write_frame(mobile,5,argv[1])==JFX_SUCCESS); check_export(argv[1]);
    assert(jfx_mobile_player_tap(mobile)==JFX_SUCCESS);
    assert(jfx_mobile_player_edit(mobile,"sequence.new",2,1,30000,1001,"")==JFX_SUCCESS);
    jfx_mobile_player_state_t playback{}; playback.size=sizeof(playback);
    assert(jfx_mobile_player_get_state(mobile,&playback)==JFX_SUCCESS);
    assert(playback.time_seconds==0 && !playback.playing);
    jfx_mobile_player_destroy(mobile);
    jfx_web_session_t *web=nullptr; assert(jfx_web_session_create(jfx_test_backend(),&web)==JFX_SUCCESS);
    assert(jfx_web_session_load_document(web,doc,n,nullptr,0)==JFX_SUCCESS);
    edit_sequence([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) { return jfx_web_session_edit(web,op,a,b,c,v,text); });
    assert(jfx_web_session_render_rgba(web,5.0/30,2,1,web_pixels,8)==JFX_SUCCESS);
    assert(jfx_web_session_sequence_state(web,web_state,sizeof(web_state))==JFX_SUCCESS);
    assert(!std::memcmp(desktop_pixels,web_pixels,8)); assert(!std::strcmp(desktop_state,web_state));
    assert(jfx_web_session_render_frame(web,5,2,1,web_pixels,8)==JFX_SUCCESS);
    assert(!std::memcmp(desktop_pixels,web_pixels,8));
    jfx_web_session_destroy(web);
}
