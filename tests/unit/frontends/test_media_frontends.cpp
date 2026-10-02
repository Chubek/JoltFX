#include "jfx/desktop_frontend.h"
#include "jfx/mobile_player.h"
#include "jfx/web_session.h"
#include "jfx/host_plugin.h"
#include "jfx_test_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
static void le(FILE *f,unsigned value,unsigned bytes) { for (unsigned i=0;i<bytes;++i) std::fputc((int)((value>>(i*8))&255),f); }
static void wave(const char *path) {
    FILE *f=std::fopen(path,"wb"); assert(f); std::fwrite("RIFF",1,4,f); le(f,36+16000,4);
    std::fwrite("WAVEfmt ",1,8,f); le(f,16,4); le(f,1,2); le(f,1,2); le(f,8000,4); le(f,16000,4); le(f,2,2); le(f,16,2);
    std::fwrite("data",1,4,f); le(f,16000,4); for (int i=0;i<8000;++i) le(f,8192,2); assert(!std::fclose(f));
}
static void check_mix(jfx_audio_mixer_t *mix) {
    assert(mix); float pcm[128]; assert(jfx_audio_mixer_render(mix,200,64,pcm,128)==JFX_SUCCESS);
    for (unsigned i=0;i<64;++i) { assert(pcm[i*2]==0.125f && pcm[i*2+1]==0.25f); }
    jfx_audio_mixer_destroy(mix);
}
static void export_job(jfx_result_t result,jfx_export_job_t *job,const char *path) {
    if (!jfx_export_available()) { assert(result==JFX_ERROR_NOT_IMPLEMENTED); return; }
    assert(result==JFX_SUCCESS && job);
    while (jfx_export_state(job)==JFX_EXPORT_RUNNING) assert(jfx_export_step(job,1)==JFX_SUCCESS);
    assert(jfx_export_completed_frames(job)==4); jfx_export_destroy(job);
    FILE *file=std::fopen(path,"rb"); assert(file); unsigned char header[4]; assert(std::fread(header,1,4,file)==4); std::fclose(file);
    assert(header[0]==0x1a && header[1]==0x45 && header[2]==0xdf && header[3]==0xa3); assert(!std::remove(path));
}
int main(int argc,char **argv) {
    assert(argc==3); wave(argv[1]);
    char document[4096]; int n=std::snprintf(document,sizeof(document),"size 16 16\nfps 25 1\ntrack \"Audio Video\"\nclip solid 0 4 0.6 0.3 0.15 1 0 0 0 0\nclip audio \"%s\" 0 4 0 0 0 0 0 0 0 0\nclip_audio 1 1 0.5 0 0 4\n",argv[1]);
    assert(n>0 && (size_t)n<sizeof(document)); jfx_audio_mixer_t *mix=nullptr; jfx_export_job_t *job=nullptr;
    char project[2048]; assert(std::snprintf(project,sizeof(project),"%s.jfx",argv[2])>0);
    FILE *file=std::fopen(project,"wb"); assert(file); assert(std::fwrite(document,1,(size_t)n,file)==(size_t)n); assert(!std::fclose(file));
    jfx_export_options_t options{}; options.size=sizeof(options); options.path=argv[2]; options.audio=true;
    jfx_desktop_frontend_config_t dc{}; dc.size=sizeof(dc); dc.width=16; dc.height=16; dc.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *desktop=nullptr; assert(jfx_desktop_frontend_create(&dc,&desktop)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(desktop,project)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_audio_mixer(desktop,8000,&mix)==JFX_SUCCESS); check_mix(mix);
    auto result=jfx_desktop_frontend_export_begin(desktop,&options,&job); export_job(result,job,argv[2]);
    jfx_desktop_frontend_destroy(desktop);
    jfx_mobile_player_config_t mc{}; mc.size=sizeof(mc); mc.width=16; mc.height=16; mc.duration_seconds=1; mc.backend_name=jfx_test_backend();
    jfx_mobile_player_t *mobile=nullptr; assert(jfx_mobile_player_create(&mc,&mobile)==JFX_SUCCESS);
    assert(jfx_mobile_player_load_document(mobile,document,(size_t)n,nullptr,0)==JFX_SUCCESS);
    assert(jfx_mobile_player_audio_mixer(mobile,8000,&mix)==JFX_SUCCESS); check_mix(mix);
    result=jfx_mobile_player_export_begin(mobile,&options,&job); export_job(result,job,argv[2]); jfx_mobile_player_destroy(mobile);
    jfx_web_session_t *web=nullptr; assert(jfx_web_session_create(jfx_test_backend(),&web)==JFX_SUCCESS);
    assert(jfx_web_session_load_document(web,document,(size_t)n,nullptr,0)==JFX_SUCCESS);
    assert(jfx_web_session_audio_mixer(web,8000,&mix)==JFX_SUCCESS); check_mix(mix);
    result=jfx_web_session_export_begin(web,argv[2],nullptr,0,0,true,&job); export_job(result,job,argv[2]); jfx_web_session_destroy(web);
    for (unsigned host=0;host<JFX_HOST_COUNT;++host) {
        jfx_host_nle_t *session=nullptr; assert(jfx_host_nle_create((jfx_host_kind_t)host,16,16,&session)==JFX_SUCCESS);
        assert(jfx_host_nle_load(session,document,(size_t)n,nullptr,0)==JFX_SUCCESS);
        assert(jfx_host_nle_audio_mixer(session,8000,&mix)==JFX_SUCCESS); check_mix(mix);
        result=jfx_host_nle_export_begin(session,&options,&job); export_job(result,job,argv[2]); jfx_host_nle_destroy(session);
    }
    assert(jfx_mobile_player_audio_mixer(nullptr,8000,&mix)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_nle_export_begin(nullptr,&options,&job)==JFX_ERROR_INVALID_ARGUMENT);
    assert(!std::remove(argv[1]) && !std::remove(project)); return 0;
}
