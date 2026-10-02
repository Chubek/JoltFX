#include "jfx/desktop_frontend.h"
#include "jfx/mobile_player.h"
#include "jfx/web_session.h"
#include "jfx_test_backend.h"
#include <cassert>
#include <cstdio>
#include <cstring>

int main() {
    const char *path = JOLT_TEST_ROOT "/tests/fixtures/color_sequence.jfx";
    char document[4096];
    FILE *file = std::fopen(path, "rb"); assert(file);
    size_t n = std::fread(document, 1, sizeof(document), file);
    std::fclose(file);
    jfx_desktop_frontend_config_t dc{};
    dc.size=sizeof(dc); dc.width=320; dc.height=180; dc.backend_name=jfx_test_backend();
    jfx_mobile_player_config_t mc{};
    mc.size=sizeof(mc); mc.width=2; mc.height=1; mc.duration_seconds=10; mc.backend_name=jfx_test_backend();
    jfx_desktop_frontend_t *desktop=nullptr;
    jfx_mobile_player_t *mobile=nullptr;
    jfx_web_session_t *web=nullptr;
    assert(jfx_desktop_frontend_create(&dc,&desktop)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_open_project(desktop,path)==JFX_SUCCESS);
    uint8_t a[8], b[8], c[8], bypass[8];
    assert(jfx_desktop_frontend_render_rgba8(desktop,2,1,a,sizeof(a))==JFX_SUCCESS);
    assert(a[0]==128 && a[1]==64 && a[2]==32 && a[3]==255);
    assert(jfx_desktop_frontend_layer_effects_set_enabled(desktop,0,0,1,false)==JFX_SUCCESS);
    assert(jfx_desktop_frontend_render_rgba8(desktop,2,1,bypass,sizeof(bypass))==JFX_SUCCESS);
    assert(bypass[0]==64 && bypass[1]==32 && bypass[2]==16 && bypass[3]==255);
    assert(jfx_desktop_frontend_panel_visible(desktop,JFX_DESKTOP_PANEL_COLOR_CALIBRATION));
    jfx_desktop_frontend_destroy(desktop);
    // The engine memory subsystem allows one owning engine at a time.
    assert(jfx_mobile_player_create(&mc,&mobile)==JFX_SUCCESS);
    assert(jfx_mobile_player_load_document(mobile,document,n,nullptr,0)==JFX_SUCCESS);
    assert(jfx_mobile_player_render_rgba8(mobile,0,b,sizeof(b))==JFX_SUCCESS);
    assert(!std::memcmp(a,b,sizeof(a)));
    // Each binding's mutation must reach the same sequence renderer.
    assert(jfx_mobile_player_edit(mobile,"grade.enabled",0,0,0,0,nullptr)==JFX_SUCCESS);
    assert(jfx_mobile_player_render_rgba8(mobile,0,b,sizeof(b))==JFX_SUCCESS);
    assert(!std::memcmp(bypass,b,sizeof(b)));
    jfx_mobile_player_destroy(mobile);
    assert(jfx_web_session_create(jfx_test_backend(),&web)==JFX_SUCCESS);
    assert(jfx_web_session_load_document(web,document,n,nullptr,0)==JFX_SUCCESS);
    assert(jfx_web_session_render_rgba(web,0,2,1,c,sizeof(c))==JFX_SUCCESS);
    assert(!std::memcmp(a,c,sizeof(a)));
    assert(jfx_web_session_edit(web,"grade.enabled",0,0,0,0,nullptr)==JFX_SUCCESS);
    assert(jfx_web_session_render_rgba(web,0,2,1,c,sizeof(c))==JFX_SUCCESS);
    assert(!std::memcmp(bypass,c,sizeof(c)));
    jfx_web_session_destroy(web);
    return 0;
}
