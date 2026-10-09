#include "jfx/desktop_frontend.h"
#include "jfx/web_session.h"
#include "jfx/mobile_player.h"
#include "jfx/host_plugin.h"
#include "jfx_test_backend.h"
#include "tilly/memory.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
int main() {
    auto *editor=jfx_editor_create(128,128); assert(editor);
    assert(jfx_editor_command(editor,"3d.add",0,0,0,0,"cube")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.key",0,4,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.key",0,4,30,90,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.cloner",0,2,3,2,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.script",0,1,0,0,"(defkernel lift [time frame index value] (+ (+ value time) (* index .25)))")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.add",0,0,0,0,"nurbs")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.nurbs_point",1,5,3,2,"")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.add",0,0,0,0,"metaball")==JFX_SUCCESS);
    assert(jfx_editor_command(editor,"3d.orbit",0,0,0,0,"10 15 20")==JFX_SUCCESS);
    tilly::vector<char> text(8*1024*1024); size_t length=0;
    assert(jfx_editor_save(editor,text.data(),text.size(),&length)==JFX_SUCCESS);
    tilly::vector<uint8_t> expected(128*128*4),actual(expected.size());
    assert(jfx_editor_render(editor,.5,128,128,expected.data(),expected.size())==JFX_SUCCESS);
    auto *file=std::fopen("modeling3d-conformance.jfx","wb"); assert(file);
    assert(std::fwrite(text.data(),1,length,file)==length); assert(!std::fclose(file));
    char state[16384];
    jfx_desktop_frontend_config_t config={}; config.size=sizeof(config); config.width=128; config.height=128; config.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *desktop=nullptr; assert(jfx_desktop_frontend_create(&config,&desktop)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(desktop,"modeling3d-conformance.jfx")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_workspace(desktop)==JFX_DESKTOP_WORKSPACE_MODELING3D);
    assert(jfx_desktop_frontend_seek(desktop,.5)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_render_rgba8(desktop,128,128,actual.data(),actual.size())==JFX_SUCCESS); assert(actual==expected);
    assert(jfx_desktop_frontend_scene3d_state(desktop,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"active\":true"));
    jfx_desktop_frontend_destroy(desktop);
    jfx_web_session_t *web=nullptr; assert(jfx_web_session_create(jfx_test_backend(),&web)==JFX_SUCCESS);
    assert(jfx_web_session_load_document(web,text.data(),length,nullptr,0)==JFX_SUCCESS);
    assert(jfx_web_session_render_rgba(web,.5,128,128,actual.data(),actual.size())==JFX_SUCCESS); assert(actual==expected);
    assert(jfx_web_session_scene3d_state(web,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"active\":true"));
    jfx_web_session_destroy(web);
    jfx_mobile_player_config_t mobileConfig={sizeof(mobileConfig),128,128,10,jfx_test_backend()}; jfx_mobile_player_t *mobile=nullptr;
    assert(jfx_mobile_player_create(&mobileConfig,&mobile)==JFX_SUCCESS);
    assert(jfx_mobile_player_load_document(mobile,text.data(),length,nullptr,0)==JFX_SUCCESS);
    assert(jfx_mobile_player_seek(mobile,.5)==JFX_SUCCESS);
    assert(jfx_mobile_player_render_rgba8(mobile,0,actual.data(),actual.size())==JFX_SUCCESS); assert(actual==expected);
    assert(jfx_mobile_player_scene3d_state(mobile,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"active\":true"));
    jfx_mobile_player_destroy(mobile);
    for (int h=0;h<JFX_HOST_COUNT;++h) {
        jfx_host_scene3d_t *host=nullptr; assert(jfx_host_scene3d_create(jfx_host_kind_t(h),128,128,&host)==JFX_SUCCESS);
        assert(jfx_host_nle_load(host,text.data(),length,nullptr,0)==JFX_SUCCESS);
        assert(jfx_host_nle_render(host,.5,128,128,actual.data(),actual.size())==JFX_SUCCESS); assert(actual==expected);
        assert(jfx_host_scene3d_state(host,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"active\":true"));
        jfx_host_nle_destroy(host);
    }
    jfx_editor_destroy(editor);
    std::remove("modeling3d-conformance.jfx");
}
