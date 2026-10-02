#include "jfx/desktop_frontend.h"
#include "jfx/mobile_player.h"
#include "jfx/web_session.h"
#include "jfx_test_backend.h"
#ifdef JFX_TEST_HOST_COMPOSITION
#include "jfx/host_plugin.h"
#endif
#include <cassert>
#include <cstdio>
#include <cstring>

template<class Edit> void edit_graph(Edit edit) {
    assert(edit("node.param",4,0,0,.5,"opacity")==JFX_SUCCESS);
    assert(edit("node.add",0,0,0,0,"exposure")==JFX_SUCCESS);
    assert(edit("node.connect",4,6,0,0,"")==JFX_SUCCESS);
    assert(edit("node.param",6,0,0,-1,"stops")==JFX_SUCCESS);
    assert(edit("node.output",6,0,0,0,"")==JFX_SUCCESS);
    assert(edit("node.duplicate",6,0,0,0,"")==JFX_SUCCESS);
    assert(edit("node.param",7,0,0,0,"stops")==JFX_SUCCESS);
    assert(edit("node.position",6,0,0,-50,"200")==JFX_SUCCESS);
    assert(edit("node.label",6,0,0,0,"Final #Look")==JFX_SUCCESS);
    assert(edit("node.remove",5,0,0,0,"")==JFX_SUCCESS);
    assert(edit("undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(edit("redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(edit("node.connect",6,4,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
}
static void check_pixels(const uint8_t *pixels) {
    const uint8_t expected[]={32,0,96,255,32,0,96,255}; assert(!std::memcmp(pixels,expected,sizeof(expected)));
}
static void check_export(const char *path) {
    FILE *file=std::fopen(path,"rb"); assert(file); unsigned char data[64];
    size_t n=std::fread(data,1,sizeof(data),file); assert(!std::fclose(file));
    const unsigned char expected[]={'P','6','\n','2',' ','1','\n','2','5','5','\n',32,0,96,32,0,96};
    assert(n==sizeof(expected) && !std::memcmp(data,expected,n)); assert(!std::remove(path));
}
int main(int argc,char **argv) {
    assert(argc==2); const char *path=JOLT_TEST_ROOT "/tests/fixtures/composition.jfx";
    assert(jfx_desktop_frontend_graph_state(nullptr,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_render_graph(nullptr,0,0,1,1,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_desktop_frontend_write_graph(nullptr,0,0,"unused")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_graph_state(nullptr,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_render_graph(nullptr,0,0,1,1,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_mobile_player_write_graph(nullptr,0,0,"unused")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_web_session_graph_state(nullptr,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_web_session_render_graph(nullptr,0,0,1,1,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_web_session_save_sequence(nullptr,nullptr,0,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
    char doc[8192],desktop_state[32768],state[32768]; uint8_t pixels[8];
    FILE *file=std::fopen(path,"rb"); assert(file); size_t n=std::fread(doc,1,sizeof(doc),file); assert(!std::fclose(file));
    jfx_desktop_frontend_config_t dc{}; dc.size=sizeof(dc); dc.width=320; dc.height=180; dc.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *d=nullptr; assert(jfx_desktop_frontend_create(&dc,&d)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(d,path)==JFX_SUCCESS);
    edit_graph([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *s) { return jfx_desktop_frontend_edit(d,op,a,b,c,v,s); });
    assert(jfx_desktop_frontend_render_graph(d,UINT32_MAX,0,2,1,pixels,8)==JFX_SUCCESS); check_pixels(pixels);
    assert(jfx_desktop_frontend_graph_state(d,desktop_state,sizeof(desktop_state))==JFX_SUCCESS);
    assert(strstr(desktop_state,"\"output\":5") && strstr(desktop_state,"Final #Look") && strstr(desktop_state,"\"x\":-50"));
    assert(jfx_desktop_frontend_write_graph(d,UINT32_MAX,0,argv[1])==JFX_SUCCESS); check_export(argv[1]);
    assert(jfx_desktop_frontend_draw(d)==JFX_SUCCESS);
    jfx_desktop_frontend_destroy(d);

    jfx_mobile_player_config_t mc{}; mc.size=sizeof(mc); mc.width=2; mc.height=1; mc.duration_seconds=10; mc.backend_name=jfx_test_backend();
    jfx_mobile_player_t *m=nullptr; assert(jfx_mobile_player_create(&mc,&m)==JFX_SUCCESS);
    assert(jfx_mobile_player_load_document(m,doc,n,nullptr,0)==JFX_SUCCESS);
    edit_graph([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *s) { return jfx_mobile_player_edit(m,op,a,b,c,v,s); });
    assert(jfx_mobile_player_render_graph(m,UINT32_MAX,0,2,1,pixels,8)==JFX_SUCCESS); check_pixels(pixels);
    assert(jfx_mobile_player_graph_state(m,state,sizeof(state))==JFX_SUCCESS && !std::strcmp(state,desktop_state));
    assert(jfx_mobile_player_write_graph(m,UINT32_MAX,0,argv[1])==JFX_SUCCESS); check_export(argv[1]);
    jfx_mobile_player_destroy(m);

    jfx_web_session_t *w=nullptr; assert(jfx_web_session_create(jfx_test_backend(),&w)==JFX_SUCCESS);
    assert(jfx_web_session_load_document(w,doc,n,nullptr,0)==JFX_SUCCESS);
    edit_graph([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *s) { return jfx_web_session_edit(w,op,a,b,c,v,s); });
    assert(jfx_web_session_render_graph(w,UINT32_MAX,0,2,1,pixels,8)==JFX_SUCCESS); check_pixels(pixels);
    assert(jfx_web_session_graph_state(w,state,sizeof(state))==JFX_SUCCESS && !std::strcmp(state,desktop_state));
    assert(jfx_web_session_edit(w,"grade.add",0,0,0,0,"grade_primary")==JFX_SUCCESS);
    char sequence[8192]; size_t sequence_length;
    assert(jfx_web_session_save_sequence(w,sequence,sizeof(sequence),&sequence_length)==JFX_SUCCESS);
    assert(std::strstr(sequence,"effect grade_primary"));
    assert(jfx_editor_kind(jfx_web_session_editor(w))==JFX_PROJECT_KIND_GRAPH);
    assert(jfx_web_session_render_graph(w,6,0,2,1,pixels,8)==JFX_SUCCESS && pixels[0]==64 && pixels[2]==191);
    assert(jfx_web_session_graph_state(w,state,sizeof(state))==JFX_SUCCESS && !std::strcmp(state,desktop_state));
    jfx_web_session_destroy(w);
#ifdef JFX_TEST_HOST_COMPOSITION
    assert(jfx_host_composition_create(JFX_HOST_COUNT,2,1,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_composition_load(nullptr,doc,n,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_composition_save(nullptr,nullptr,0,nullptr)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_composition_state(nullptr,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_composition_edit(nullptr,"undo",0,0,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_composition_render(nullptr,0,0,1,1,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_host_composition_write_frame(nullptr,0,0,1,1,"unused")==JFX_ERROR_INVALID_ARGUMENT);
    jfx_host_composition_destroy(nullptr);
    for (int host=0;host<JFX_HOST_COUNT;++host) {
        jfx_host_composition_t *h=nullptr; assert(jfx_host_composition_create((jfx_host_kind_t)host,2,1,&h)==JFX_SUCCESS);
        assert(jfx_host_composition_load(h,doc,n,nullptr,0)==JFX_SUCCESS);
        edit_graph([&](const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *s) { return jfx_host_composition_edit(h,op,a,b,c,v,s); });
        assert(jfx_host_composition_render(h,UINT32_MAX,0,2,1,pixels,8)==JFX_SUCCESS); check_pixels(pixels);
        assert(jfx_host_composition_state(h,state,sizeof(state))==JFX_SUCCESS && !std::strcmp(state,desktop_state));
        assert(jfx_host_composition_write_frame(h,UINT32_MAX,0,2,1,argv[1])==JFX_SUCCESS); check_export(argv[1]);
        char saved[8192]; size_t length;
        assert(jfx_host_composition_save(h,saved,sizeof(saved),&length)==JFX_SUCCESS);
        assert(jfx_host_composition_load(h,"fps 30 1\ntrack V1\n",18,nullptr,0)==JFX_ERROR_INVALID_ARGUMENT);
        assert(jfx_host_composition_state(h,state,sizeof(state))==JFX_SUCCESS && !std::strcmp(state,desktop_state));
        assert(jfx_host_composition_load(h,saved,length,nullptr,0)==JFX_SUCCESS);
        assert(jfx_host_composition_render(h,UINT32_MAX,0,2,1,pixels,8)==JFX_SUCCESS); check_pixels(pixels);
        jfx_host_composition_destroy(h);
    }
#endif
}
