#include "host_window.h"
#include "jfx/jfx_recording.h"
#include "imgui.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstring>
int main(int argc,char **argv) {
    assert(argc==3);
    ImGui::CreateContext();
    jfx_desktop_window_config_t config{320,240,"DAW device integration",true}; char error[512];
    auto *window=jfx_desktop_window_create(&config,error,sizeof(error));
    if (!window) { std::fprintf(stderr,"Window unavailable: %s\n",error); ImGui::DestroyContext(); return 77; }
    jfx_vst3_class_t classes[8]; size_t count=0; assert(jfx_vst3_classes(argv[1],classes,8,&count)==JFX_SUCCESS);
    jfx_audio_insert_t in{}; in.size=sizeof(in); in.enabled=true;
    std::snprintf(in.path,sizeof(in.path),"%s",argv[1]); std::snprintf(in.cid,sizeof(in.cid),"%s",classes[0].cid);
    jfx_vst3_instance_t *plugin=nullptr; assert(jfx_vst3_create(&in,48000,4096,&plugin)==JFX_SUCCESS);
    auto attached=jfx_desktop_window_plugin_open(window,plugin,"Test native view");
    /* SDL Wayland/offscreen has no VST3 parent type. X11/HWND/NSView attach. */
    assert(attached==JFX_SUCCESS || attached==JFX_ERROR_NOT_IMPLEMENTED);
    if (attached==JFX_SUCCESS) {
        assert(jfx_desktop_window_plugin_visible(window));
        jfx_vst3_editor_wheel(plugin,1); jfx_vst3_pump();
        jfx_vst3_parameter_t p{}; p.size=sizeof(p); assert(jfx_vst3_parameter(plugin,0,&p)==JFX_SUCCESS && p.value==.25);
    }
    jfx_desktop_window_plugin_close(window); assert(!jfx_desktop_window_plugin_visible(window)); jfx_vst3_destroy(plugin);
    assert(jfx_desktop_window_input_count()>0);
    assert(jfx_desktop_window_capture_begin(window,-1));
    assert(!jfx_desktop_window_capture_begin(window,-1));
    jfx_audio_recording_t *recording=nullptr; assert(jfx_audio_recording_begin(argv[2],48000,&recording)==JFX_SUCCESS);
    auto until=std::chrono::steady_clock::now()+std::chrono::milliseconds(120); uint64_t captured=0;
    float pcm[8192];
    while (std::chrono::steady_clock::now()<until) {
        uint32_t n=0; assert(jfx_desktop_window_capture_read(window,pcm,4096,&n));
        if (n) { assert(jfx_audio_recording_push(recording,pcm,n)==JFX_SUCCESS); captured+=n; }
    }
    assert(captured>0 && jfx_audio_recording_frames(recording)==captured);
    jfx_desktop_window_capture_end(window);
    uint32_t n=0; assert(!jfx_desktop_window_capture_read(window,pcm,4096,&n));
    assert(jfx_audio_recording_finish(recording)==JFX_SUCCESS); jfx_audio_recording_destroy(recording);
    assert(!std::remove(argv[2]));
    jfx_desktop_window_destroy(window); ImGui::DestroyContext(); return 0;
}
