/* Desktop frontend.
 *
 * Composes the JoltFX editor UI with Dear ImGui and drives it from a real
 * frame loop. Everything the UI shows is real state:
 *
 *   - the viewport image is produced by rendering the selected effect through
 *     the Core engine's selected backend and uploading the result as a texture;
 *   - the timeline advances the engine clock while playing and loops at the end;
 *   - the properties panel edits the live effect and its parameter;
 *   - the console is a sink on the Tilly logger, so it shows real engine output;
 *   - the status bar reports the backend and whether the last frame ran on a GPU.
 *
 * The frontend links no SDL or GL symbol directly; the platform half lives in
 * host_window.cpp. That is what lets the same code run headless, which is the
 * path the `--headless-smoke` mode and the unit test use. */

#include "jfx/desktop_frontend.h"

#include "jfx/jfx_events.h"
#include "jfx/jfx_editor.h"
#include "jfx/jfx_color.h"
#include "jfx/jfx_vst3.h"
#include "jfx/jfx_midi.h"
#include "jfx/jfx_recording.h"
#include "jfx/jfx_automation.h"

#include "host_window.h"
#include "animation_drawing.h"
#include <new>
#include "imgui.h"
#include "joltscript/effects.h"
#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include "tilly/allocator.h"
#include "tilly/memory.h"
#include "tilly/logger.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace {

constexpr uint32_t kMaxProjectPath = 512;
constexpr uint32_t kMaxEffectName = 32;
constexpr uint32_t kPreviewWidth = 320;
constexpr uint32_t kPreviewHeight = 180;
constexpr size_t kConsoleLines = 256;
constexpr size_t kConsoleLineLength = 224;

/* Bounded log capture. A fixed ring with no allocation keeps the sink usable
 * from the scheduler's worker threads and from a sink installed before the
 * frontend exists. */
struct ConsoleState {
    std::mutex mutex;
    char lines[kConsoleLines][kConsoleLineLength];
    uint32_t level[kConsoleLines];
    uint32_t next;
    uint32_t total;
};

ConsoleState g_console;
std::mutex g_console_owner_mutex;
jfx_desktop_frontend_t *g_console_owner = nullptr;

const char *level_prefix(tilly_log_level_t level) {
    switch (level) {
    case TILLY_LOG_ERROR:
    case TILLY_LOG_FATAL:
        return "error";
    case TILLY_LOG_WARN:
        return "warn";
    case TILLY_LOG_INFO:
        return "info";
    case TILLY_LOG_DEBUG:
        return "debug";
    case TILLY_LOG_TRACE:
    default:
        return "trace";
    }
}

void console_push(tilly_log_level_t level, const char *message) {
    std::lock_guard<std::mutex> guard(g_console.mutex);
    uint32_t slot = g_console.next;
    std::snprintf(g_console.lines[slot], kConsoleLineLength, "%s",
        (message && message[0]) ? message : "(no message)");
    g_console.level[slot] = (uint32_t)level;
    g_console.next = (g_console.next + 1u) % (uint32_t)kConsoleLines;
    g_console.total++;
}

void console_sink(const tilly_log_entry_t *entry, void *user_data) {
    (void)user_data;
    jfx_desktop_frontend_t *owner = nullptr;
    {
        std::lock_guard<std::mutex> guard(g_console_owner_mutex);
        owner = g_console_owner;
    }
    /* Only capture while a frontend is alive to show it. */
    if (owner) {
        char line[kConsoleLineLength];
        std::snprintf(line, sizeof(line), "[%s] %s", level_prefix(entry->level), entry->message);
        console_push(entry->level, line);
    }
}

float clampf(float value, float lo, float hi) {
    if (!(value == value)) {
        return lo; /* NaN */
    }
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

uint8_t to_unorm8(float value) {
    if (!(value > 0.0f)) return 0u; /* also catches NaN */
    if (value >= 1.0f) return 255u;
    return (uint8_t)std::lrintf(value * 255.0f);
}

/* Resolves a writable ImGui settings path in the platform's per-user config
 * directory, so window positions persist without ever writing into the source
 * tree. Returns nullptr when no suitable directory exists, which leaves ImGui
 * running without persistence. */
const char *imgui_ini_path(void) {
    static char path[512];
    static bool resolved = false;
    static bool have_path = false;
    if (resolved) {
        return have_path ? path : nullptr;
    }
    resolved = true;
    std::filesystem::path directory;
#if defined(_WIN32)
    if (const char *appdata = std::getenv("APPDATA")) {
        directory = std::filesystem::path(appdata) / "JoltFX";
    }
#elif defined(__APPLE__)
    if (const char *home = std::getenv("HOME")) {
        directory = std::filesystem::path(home) / "Library" / "Application Support" / "JoltFX";
    }
#else
    if (const char *xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg[0] == '/') {
            directory = std::filesystem::path(xdg) / "joltfx";
        }
    }
    if (directory.empty()) {
        if (const char *home = std::getenv("HOME")) {
            directory = std::filesystem::path(home) / ".config" / "joltfx";
        }
    }
#endif
    if (directory.empty()) {
        return nullptr;
    }
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return nullptr;
    }
    const std::filesystem::path file = directory / "imgui.ini";
    std::snprintf(path, sizeof(path), "%s", file.string().c_str());
    have_path = path[0] != '\0';
    return have_path ? path : nullptr;
}

} // namespace

struct jfx_desktop_frontend {
    jfx_engine_t *engine;
    jfx_editor_t *editor;
    jfx_plugin_host_t *plugins;
    char plugin_path[512];
    bool editing, looping, show_grade, show_nodes;
    bool show_calibration;
    bool show_plugins, show_audio, show_daw, workspace_requested;
    bool show_animation;
    bool show_modeling3d;
    bool show_creative;
    int scene_object,scene_vertex,scene_channel,scene_interpolation;
    int scene_bake_frames;
    int scene_primitive,scene_segments,scene_control,scene_ball,scene_gizmo_axis;
    int scene_clone_mode,scene_clone_count;
    float scene_clone_spacing;
    bool scene_navigation;
    char scene_script[4097];
    char scene_script_path[512];
    char scene_mesh_path[512],scene_png_path[512];
    jfx_desktop_workspace_t workspace;
    bool grading_edit;
    bool audio_edit;
    uint32_t selected_track, selected_clip, selected_node;
    float nle_zoom, nle_drag_x;
    uint64_t nle_first, nle_drag_start, nle_drag_length;
    uint32_t nle_drag_track, nle_drag_clip, nle_drop_track;
    int nle_drag;
    bool nle_snap;
    int nle_width, nle_height, nle_fps_num, nle_fps_den;
    int nle_gap_length;
    char nle_export_path[512];
    char video_export_path[512], video_codec[64];
    uint64_t video_start,video_frames;
    bool video_audio;
    jfx_export_job_t *export_job;
    jfx_audio_mixer_t *audio_mixer;
    jfx_animation_tab_t *animation_tab;
    jfx_drawing::Editor *drawing;
    uint64_t audio_sample;
    float audio_peak[2];
    int audio_import_frames;
    char vst3_path[JFX_NODE_PATH_MAX];
    struct { char path[JFX_NODE_PATH_MAX]; jfx_vst3_class_t info; } vst3_catalog[128];
    uint32_t vst3_count, vst3_selected, vst3_insert;
    jfx_vst3_instance_t *vst3_inspector;
    char vst3_inspector_path[JFX_NODE_PATH_MAX],vst3_inspector_cid[33];
    char audio_export_path[512];
    uint32_t vst3_inspector_track,vst3_inspector_insert;
    bool native_dirty,native_edit,native_session;
    uint32_t native_gesture;
    jfx_audio_recording_t *recording;
    char recording_path[512];
    int recording_device;
    uint32_t recording_track;
    uint64_t recording_start;
    int midi_pitch,midi_channel;
    uint64_t midi_frame,midi_length;
    float midi_velocity,input_peak[2];
    int automation_target,automation_insert,automation_interpolation;
    uint32_t automation_parameter;
    uint64_t automation_frame;
    float automation_value;
    float node_zoom, node_pan_x, node_pan_y, node_drag_x, node_drag_y, node_start_x, node_start_y;
    int node_drag;
    uint32_t node_wire_source, node_wire_port;
    bool node_wiring;
    bool node_preview_selected;
    char node_filter[64], node_export_path[512];
    char lut_path[512], media_path[512];
    jfx_lut_t *grade_lut;
    float lut_mix, lift[3], gamma[3], gain[3];
    jolt_effects_t *effects;
    ImGuiContext *imgui;
    jfx_desktop_window_t *window;

    uint32_t width;
    uint32_t height;
    double workspace_zoom;
    double time_seconds;
    double duration_seconds;
    bool playing;
    uint64_t frame_count;

    char project_path[kMaxProjectPath];
    char effect_name[kMaxEffectName];
    float effect_parameter;
    float effect_min;
    float effect_max;
    bool effect_has_parameter;

    /* Creative-programming tab (Zoltan sketches). The source is one JBC1
     * `(defkernel [x y t ...] body)` program; preview compiles with the Glue
     * compiler and evaluates per pixel with the VM. Bounded so preview and
     * export stay cheap: source < 8 KiB, raster <= 320x180. */
    char creative_source[8192];
    char creative_error[512];
    char creative_export_path[512];
    bool creative_show_code;
    bool creative_show_preview;
    bool creative_playing;
    bool creative_dirty;
    float creative_t;
    float creative_mx;
    float creative_my;
    int creative_width;
    int creative_height;
    jfx_result_t creative_status;

    /* Preview buffers. Allocated once at the preview resolution and reused
     * every frame so playback does not churn the allocator. */
    float *preview_input;
    float *preview_float;
    uint8_t *preview_rgba;
    uint8_t *scene_rgba;
    uint32_t scene_width,scene_height;
    bool preview_dirty;
    void *preview_texture; /* host-owned handle for ImGui::Image */
    char status[160];
    bool show_viewport;
    bool show_timeline;
    bool show_properties;
    bool show_console;
    bool show_stats;
    bool quit_requested;
    bool layout_initialized;
    char project_input[kMaxProjectPath];
};

static void reset_audio(jfx_desktop_frontend_t *f) {
    jfx_audio_mixer_destroy(f->audio_mixer); f->audio_mixer=nullptr;
    jfx_desktop_window_clear_audio(f->window);
    f->audio_peak[0]=f->audio_peak[1]=0;
}
namespace {
void finish_grading(jfx_desktop_frontend_t *f); void finish_audio(jfx_desktop_frontend_t *f);
jfx_result_t native_sync(jfx_desktop_frontend_t *f,bool finish);
void close_inspector(jfx_desktop_frontend_t *f);
void recording_poll(jfx_desktop_frontend_t *f);
}

/* Mirrors engine events into the console so the panel shows what the engine is
 * actually doing rather than a fixed string. */
void engine_event_sink(jfx_event_type_t type, void *data, void *user_data) {
    (void)data;
    (void)user_data;
    char line[kConsoleLineLength];
    switch (type) {
    case JFX_EVENT_KERNEL_SUBMIT:
        console_push(TILLY_LOG_DEBUG, "[engine] kernel submitted");
        break;
    case JFX_EVENT_KERNEL_COMPLETE:
        console_push(TILLY_LOG_DEBUG, "[engine] kernel complete");
        break;
    case JFX_EVENT_KERNEL_ERROR:
        console_push(TILLY_LOG_ERROR, "[engine] kernel error");
        break;
    case JFX_EVENT_RESOURCE_ALLOC:
        console_push(TILLY_LOG_DEBUG, "[engine] resource allocated");
        break;
    case JFX_EVENT_RESOURCE_FREE:
        console_push(TILLY_LOG_DEBUG, "[engine] resource released");
        break;
    case JFX_EVENT_PLUGIN_LOAD:
        std::snprintf(line, sizeof(line), "[engine] plugin loaded");
        console_push(TILLY_LOG_INFO, line);
        break;
    case JFX_EVENT_PLUGIN_UNLOAD:
        std::snprintf(line, sizeof(line), "[engine] plugin unloaded");
        console_push(TILLY_LOG_INFO, line);
        break;
    default:
        break;
    }
}

static void *imgui_alloc(size_t bytes, void *) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        alignof(max_align_t));
}

static void imgui_free(void *pointer, void *) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), pointer);
}

extern "C" jfx_result_t jfx_desktop_frontend_create(
    const jfx_desktop_frontend_config_t *config, jfx_desktop_frontend_t **out_frontend) {
    if (!config || !out_frontend) return JFX_ERROR_INVALID_ARGUMENT;
    *out_frontend = nullptr;

    jfx_desktop_frontend_t *frontend = static_cast<jfx_desktop_frontend_t *>(tilly_alloc(
        (tilly_allocator_t *)tilly_default_allocator(), sizeof(*frontend),
        alignof(jfx_desktop_frontend_t)));
    if (!frontend) return JFX_ERROR_OUT_OF_MEMORY;
    std::memset(frontend, 0, sizeof(*frontend));

    frontend->width = (config && config->width) ? config->width : JFX_DESKTOP_DEFAULT_WIDTH;
    frontend->height = (config && config->height) ? config->height : JFX_DESKTOP_DEFAULT_HEIGHT;
    frontend->duration_seconds = JFX_DESKTOP_DEFAULT_DURATION;
    frontend->workspace_zoom = 1.0;
    frontend->show_viewport = true;
    frontend->show_timeline = true;
    frontend->show_properties = true;
    frontend->show_grade = true; frontend->show_nodes = true; frontend->looping = true;
    frontend->show_calibration = true;
    frontend->show_plugins = true;
    frontend->show_audio = true;
    frontend->show_daw = true;
    frontend->show_animation = true;
    frontend->show_modeling3d = true;
    frontend->show_creative = true;
    frontend->scene_bake_frames=60;
    frontend->scene_segments=64;
    frontend->scene_clone_count=5;
    frontend->scene_clone_spacing=3;
    std::snprintf(frontend->scene_script,sizeof(frontend->scene_script),"(defkernel spin [time frame index value] (+ value (* time 90)))");
    std::snprintf(frontend->scene_script_path,sizeof(frontend->scene_script_path),"examples/modeling3d/spin.jolt");
    std::snprintf(frontend->scene_mesh_path,sizeof(frontend->scene_mesh_path),"mesh.ply");
    std::snprintf(frontend->scene_png_path,sizeof(frontend->scene_png_path),"scene.png");
    std::snprintf(frontend->audio_export_path,sizeof(frontend->audio_export_path),"mix.wav");
    std::snprintf(frontend->creative_source, sizeof(frontend->creative_source),
        ";; @kernel sketch_demo\n;; @category generative\n"
        ";; @description Creative sketch scaffold.\n;; @complexity Low\n"
        ";; @gpu Yes\n;; @since 0.3.0\n;; @canvas 320x180\n"
        "(defkernel sketch_demo [x y t]\n"
        "  (rgba x y (* 0.5 (+ 0.5 (* 0.5 t))) 1.0))");
    frontend->creative_show_code = true;
    frontend->creative_show_preview = true;
    frontend->creative_playing = false;
    frontend->creative_dirty = true;
    frontend->creative_t = 0.0f;
    frontend->creative_mx = 0.5f;
    frontend->creative_my = 0.5f;
    frontend->creative_width = 320;
    frontend->creative_height = 180;
    frontend->creative_status = JFX_SUCCESS;
    std::snprintf(frontend->creative_export_path, sizeof(frontend->creative_export_path), "sketch.o");
    frontend->audio_import_frames = 90;
    frontend->recording_device=-1;
    std::snprintf(frontend->recording_path,sizeof(frontend->recording_path),"take.wav");
    frontend->midi_pitch=60; frontend->midi_length=15; frontend->midi_velocity=.8f;
    frontend->automation_value=1;
    frontend->nle_width=320; frontend->nle_height=180; frontend->nle_fps_num=30; frontend->nle_fps_den=1;
    frontend->nle_gap_length=30;
    std::snprintf(frontend->nle_export_path,sizeof(frontend->nle_export_path),"frame.ppm");
    std::snprintf(frontend->video_export_path,sizeof(frontend->video_export_path),"sequence.mp4");
    frontend->video_audio=true;
    std::snprintf(frontend->node_export_path,sizeof(frontend->node_export_path),"composition.ppm");
    frontend->lut_mix = 1;
    for (int i=0; i<3; ++i) frontend->gamma[i] = frontend->gain[i] = 1;
    frontend->show_console = true;
    frontend->preview_dirty = true;
    std::snprintf(frontend->status, sizeof(frontend->status), "starting up");

    jfx_engine_config_t engine_config{};
    engine_config.max_buffers = 256;
    engine_config.max_textures = 64;
    engine_config.max_kernels = 32;
    engine_config.backend_name = config ? config->backend_name : nullptr;
    jfx_result_t result = jfx_engine_init(&engine_config, &frontend->engine);
    if (result != JFX_SUCCESS) {
        /* The engine already logged why; do not duplicate it here. */
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return result;
    }

    frontend->effects = jolt_effects_create();
    if (!frontend->effects) {
        jfx_engine_shutdown(frontend->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    frontend->preview_input = static_cast<float *>(tilly_alloc(
        (tilly_allocator_t *)tilly_default_allocator(), (size_t)kPreviewWidth * kPreviewHeight * 4u *
            sizeof(float), alignof(float)));
    frontend->preview_float = static_cast<float *>(tilly_alloc(
        (tilly_allocator_t *)tilly_default_allocator(), (size_t)kPreviewWidth * kPreviewHeight * 4u *
            sizeof(float), alignof(float)));
    frontend->preview_rgba = static_cast<uint8_t *>(tilly_alloc(
        (tilly_allocator_t *)tilly_default_allocator(), (size_t)kPreviewWidth * kPreviewHeight * 4u,
        alignof(uint8_t)));
    if (!frontend->preview_input || !frontend->preview_float || !frontend->preview_rgba) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_input);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_float);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_rgba);
        jolt_effects_destroy(frontend->effects);
        jfx_engine_shutdown(frontend->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    ImGui::SetAllocatorFunctions(imgui_alloc, imgui_free, nullptr);
    frontend->imgui = ImGui::CreateContext();
    if (!frontend->imgui) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_input);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_float);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_rgba);
        jolt_effects_destroy(frontend->effects);
        jfx_engine_shutdown(frontend->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    ImGui::SetCurrentContext(frontend->imgui);
    ImGui::StyleColorsDark();
    /* The font atlas is deliberately not built here. The renderer backend
     * (ImGui_ImplOpenGL3) owns font texture creation and flags the atlas as
     * renderer-backed when it initializes; building it first would trip
     * ImFontAtlas::Build's "RendererHasTextures" assertion on the next frame. */
    /* Layout persists in the platform's per-user config directory, never in the
     * source tree. */
    ImGui::GetIO().IniFilename = imgui_ini_path();
    frontend->editor = jfx_editor_create(kPreviewWidth, kPreviewHeight);
    if (!frontend->editor) { jfx_desktop_frontend_destroy(frontend); return JFX_ERROR_OUT_OF_MEMORY; }
    frontend->animation_tab = jfx_animation_tab_create(kPreviewWidth, kPreviewHeight);
    if (!frontend->animation_tab) { jfx_desktop_frontend_destroy(frontend); return JFX_ERROR_OUT_OF_MEMORY; }
    frontend->drawing=tilly::create<jfx_drawing::Editor>();
    if (!frontend->drawing) { jfx_desktop_frontend_destroy(frontend); return JFX_ERROR_OUT_OF_MEMORY; }
    result=jfx_plugin_host_create(frontend->engine,&frontend->plugins);
    if (result!=JFX_SUCCESS) { jfx_desktop_frontend_destroy(frontend); return result; }

    {
        std::lock_guard<std::mutex> guard(g_console_owner_mutex);
        g_console_owner = frontend;
    }
    tilly_log_add_sink(console_sink, nullptr);
    event_subscribe(JFX_EVENT_KERNEL_SUBMIT, engine_event_sink, nullptr);
    event_subscribe(JFX_EVENT_KERNEL_COMPLETE, engine_event_sink, nullptr);
    event_subscribe(JFX_EVENT_KERNEL_ERROR, engine_event_sink, nullptr);
    event_subscribe(JFX_EVENT_RESOURCE_ALLOC, engine_event_sink, nullptr);
    event_subscribe(JFX_EVENT_RESOURCE_FREE, engine_event_sink, nullptr);
    event_subscribe(JFX_EVENT_PLUGIN_LOAD, engine_event_sink, nullptr);
    event_subscribe(JFX_EVENT_PLUGIN_UNLOAD, engine_event_sink, nullptr);

    const char *effect = (config && config->effect_name && config->effect_name[0])
        ? config->effect_name
        : "brightness";
    if (jfx_desktop_frontend_set_effect(frontend, effect,
            config ? config->effect_parameter : 0.0f) != JFX_SUCCESS) {
        jfx_desktop_frontend_set_effect(frontend, "brightness", 0.0f);
    }
    if (config && config->project_path && config->project_path[0]) {
        jfx_desktop_frontend_open_project(frontend, config->project_path);
    }

    *out_frontend = frontend;
    return JFX_SUCCESS;
}

extern "C" void jfx_desktop_frontend_destroy(jfx_desktop_frontend_t *frontend) {
    if (frontend) {
        jfx_desktop_window_capture_end(frontend->window); jfx_audio_recording_destroy(frontend->recording);
        close_inspector(frontend); jfx_export_destroy(frontend->export_job); reset_audio(frontend);
        jfx_animation_tab_destroy(frontend->animation_tab);
        tilly::destroy(frontend->drawing);
    }
    if (!frontend) return;
    event_unsubscribe(JFX_EVENT_KERNEL_SUBMIT, engine_event_sink);
    event_unsubscribe(JFX_EVENT_KERNEL_COMPLETE, engine_event_sink);
    event_unsubscribe(JFX_EVENT_KERNEL_ERROR, engine_event_sink);
    event_unsubscribe(JFX_EVENT_RESOURCE_ALLOC, engine_event_sink);
    event_unsubscribe(JFX_EVENT_RESOURCE_FREE, engine_event_sink);
    event_unsubscribe(JFX_EVENT_PLUGIN_LOAD, engine_event_sink);
    event_unsubscribe(JFX_EVENT_PLUGIN_UNLOAD, engine_event_sink);
    {
        std::lock_guard<std::mutex> guard(g_console_owner_mutex);
        g_console_owner = nullptr;
    }
    tilly_log_remove_sink(console_sink);
    jfx_desktop_window_destroy(frontend->window);
    if (frontend->imgui) {
        ImGui::SetCurrentContext(frontend->imgui);
        ImGui::DestroyContext(frontend->imgui);
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_input);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_float);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend->preview_rgba);
    tilly_mem_free(frontend->scene_rgba);
    jfx_editor_destroy(frontend->editor);
    jfx_plugin_host_destroy(frontend->plugins);
    jfx_lut_destroy(frontend->grade_lut);
    jolt_effects_destroy(frontend->effects);
    jfx_engine_shutdown(frontend->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
}

extern "C" jfx_result_t jfx_desktop_frontend_open_project(jfx_desktop_frontend_t *frontend,
    const char *path) {
    if (!frontend || !path || !path[0] || std::strlen(path) >= sizeof(frontend->project_path)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (frontend->recording) return JFX_ERROR_BUSY;
    auto synced=native_sync(frontend,true); if (synced!=JFX_SUCCESS) return synced;
    close_inspector(frontend);
    finish_grading(frontend);
    if (const char *ext = std::strrchr(path, '.'); ext && std::strcmp(ext, ".jfx") == 0) {
        FILE *file = std::fopen(path, "rb");
        if (!file) return JFX_ERROR_NOT_FOUND;
        char *text = static_cast<char *>(imgui_alloc(JFX_PROJECT_MAX_BYTES + 1, nullptr));
        if (!text) { std::fclose(file); return JFX_ERROR_OUT_OF_MEMORY; }
        size_t n = std::fread(text, 1, JFX_PROJECT_MAX_BYTES + 1, file);
        bool failed = std::ferror(file) != 0; std::fclose(file);
        jfx_result_t result = failed ? JFX_ERROR_INVALID_ARGUMENT : jfx_editor_load(frontend->editor,
            text, n, frontend->status, sizeof(frontend->status));
        imgui_free(text, nullptr);
        if (result != JFX_SUCCESS) return result;
        reset_audio(frontend);
        frontend->editing = true; frontend->selected_track = frontend->selected_clip = frontend->selected_node = 0;
        frontend->node_preview_selected=false; frontend->node_drag=0; frontend->node_wiring=false;
        frontend->time_seconds = 0; frontend->preview_dirty = true;
        jfx_desktop_frontend_set_workspace(frontend,jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_GRAPH?
            JFX_DESKTOP_WORKSPACE_COMPOSITING:jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_SCENE3D?
            JFX_DESKTOP_WORKSPACE_MODELING3D:JFX_DESKTOP_WORKSPACE_NLE);
    }
    std::strcpy(frontend->project_path, path);
    std::snprintf(frontend->project_input, sizeof(frontend->project_input), "%s", path);
    if (const char *dot = std::strrchr(path, '.'); dot && !std::strcmp(dot, ".jolt")) {
        FILE *kernel = std::fopen(path, "rb");
        if (kernel) {
            size_t n = std::fread(frontend->creative_source, 1,
                sizeof(frontend->creative_source) - 1, kernel);
            bool failed = std::ferror(kernel) != 0;
            std::fclose(kernel);
            if (!failed && n < sizeof(frontend->creative_source) - 1) {
                frontend->creative_source[n] = 0;
                frontend->creative_error[0] = 0;
                jfx_desktop_frontend_set_workspace(frontend, JFX_DESKTOP_WORKSPACE_CREATIVE);
            }
        }
    }
    std::snprintf(frontend->status, sizeof(frontend->status), "opened %s", path);
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_resize(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height) {
    if (!frontend || !width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    frontend->width = width;
    frontend->height = height;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_set_zoom(jfx_desktop_frontend_t *frontend,
    double zoom) {
    if (!frontend || !std::isfinite(zoom) || zoom < 0.25 || zoom > 8.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    frontend->workspace_zoom = zoom;
    frontend->preview_dirty = true;
    return JFX_SUCCESS;
}

extern "C" double jfx_desktop_frontend_zoom(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->workspace_zoom : 0.0;
}

extern "C" jfx_result_t jfx_desktop_frontend_play(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    if (frontend->recording) return JFX_ERROR_BUSY;
    frontend->playing = true;
    jfx_animation_tab_set_playing(frontend->animation_tab, true);
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_pause(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    reset_audio(frontend);
    frontend->playing = false;
    jfx_animation_tab_set_playing(frontend->animation_tab, false);
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds) {
    if (!frontend || !std::isfinite(time_seconds) || time_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    reset_audio(frontend); frontend->time_seconds = time_seconds;
    jfx_animation_tab_set_time(frontend->animation_tab, time_seconds);
    frontend->preview_dirty = true;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_set_effect(jfx_desktop_frontend_t *frontend,
    const char *effect_name, float parameter) {
    if (!frontend || !effect_name || !effect_name[0] ||
        std::strlen(effect_name) >= sizeof(frontend->effect_name) ||
        !jolt_effects_exists(frontend->effects, effect_name)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (jolt_effects_parameter_count(frontend->effects, effect_name) == 0 &&
        parameter != 0.0f) {
        /* Effects with no parameters reject a parameter rather than ignore it. */
        if (!std::isfinite(parameter)) return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!std::isfinite(parameter)) return JFX_ERROR_INVALID_ARGUMENT;
    frontend->effect_has_parameter =
        jolt_effects_parameter_count(frontend->effects, effect_name) > 0;
    jolt_effects_parameter_range(frontend->effects, effect_name, &frontend->effect_min,
        &frontend->effect_max);
    frontend->effect_parameter =
        frontend->effect_has_parameter ? clampf(parameter, frontend->effect_min, frontend->effect_max)
                                       : 0.0f;
    std::snprintf(frontend->effect_name, sizeof(frontend->effect_name), "%s", effect_name);
    frontend->preview_dirty = true;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_render_rgba8(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height, uint8_t *out_rgba, size_t out_size) {
    if (!frontend || !width || !height || !out_rgba) return JFX_ERROR_INVALID_ARGUMENT;
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / (4u * sizeof(float))) return JFX_ERROR_OUT_OF_MEMORY;
    if (out_size < pixels * 4u) return JFX_ERROR_INVALID_ARGUMENT;

    if (frontend->editing) {
        if (frontend->node_preview_selected && jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_GRAPH)
            return jfx_editor_render_graph(frontend->editor,frontend->selected_node,frontend->time_seconds,width,height,out_rgba,out_size);
        return jfx_editor_render(frontend->editor, frontend->time_seconds, width, height, out_rgba, out_size);
    }

    /* Only the legacy effect preview uses fixed-size scratch buffers. Shared
     * documents allocate their own rasters and validate their own size limits. */
    if (width > kPreviewWidth || height > kPreviewHeight) return JFX_ERROR_INVALID_ARGUMENT;

    /* An animated gradient: this is the input the effect is previewed on, and
     * it is what makes a moving blue channel visible while playing. */
    const double phase = frontend->time_seconds - std::floor(frontend->time_seconds);
    const float fphase = (float)phase;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t index = ((size_t)y * width + x) * 4u;
            frontend->preview_input[index] =
                (float)x / (float)(width > 1u ? width - 1u : 1u);
            frontend->preview_input[index + 1u] =
                (float)y / (float)(height > 1u ? height - 1u : 1u);
            frontend->preview_input[index + 2u] = fphase;
            frontend->preview_input[index + 3u] = 1.0f;
        }
    }

    size_t bytecode_size = 0;
    const uint8_t *bytecode =
        jolt_effects_bytecode(frontend->effects, frontend->effect_name, &bytecode_size);
    if (!bytecode || !bytecode_size) return JFX_ERROR_NOT_FOUND;

    const float *parameters = frontend->effect_has_parameter ? &frontend->effect_parameter : nullptr;
    const size_t parameter_count = frontend->effect_has_parameter ? 1u : 0u;
    jfx_result_t status = jfx_engine_execute_bytecode(frontend->engine, bytecode, bytecode_size,
        frontend->preview_input, pixels, parameters, parameter_count, frontend->preview_float);
    if (status != JFX_SUCCESS) {
        std::snprintf(frontend->status, sizeof(frontend->status), "render failed (%d)",
            (int)status);
        return status;
    }
    for (size_t i = 0; i < pixels * 4u; ++i) {
        out_rgba[i] = to_unorm8(frontend->preview_float[i]);
    }
    std::snprintf(frontend->status, sizeof(frontend->status), "rendered %ux%u %s", width, height,
        frontend->effect_name);
    return JFX_SUCCESS;
}

namespace {

#include "editor_panels.inc"
#include "daw_features.inc"
#include "audio_panel.inc"

constexpr const char *workspace_names[]={"NLE","Layer Effects","Color Calibration","Color Grading",
    "Node Compositing","Plugins","Console","Statistics","Audio Mixing","DAW","2D Animation","3D Modeling & Animation",
    "Creative Programming"};
constexpr jfx_desktop_panel_t workspace_panels[]={JFX_DESKTOP_PANEL_TIMELINE,JFX_DESKTOP_PANEL_LAYER_EFFECTS,
    JFX_DESKTOP_PANEL_COLOR_CALIBRATION,JFX_DESKTOP_PANEL_COLOR_GRADING,JFX_DESKTOP_PANEL_NODE_COMPOSITING,
    JFX_DESKTOP_PANEL_PLUGINS,JFX_DESKTOP_PANEL_CONSOLE,JFX_DESKTOP_PANEL_STATISTICS,JFX_DESKTOP_PANEL_AUDIO,
    JFX_DESKTOP_PANEL_DAW,JFX_DESKTOP_PANEL_ANIMATION,JFX_DESKTOP_PANEL_MODELING3D,
    JFX_DESKTOP_PANEL_CREATIVE};

#include "animation_panel.inc"
#include "modeling3d_panel.inc"

/* Creative-sketch headless logic. Sketches run through the Glue compiler and
 * the VM directly (per pixel, explicit bindings), which supports any input
 * list including [x y t mx my]. The engine pipeline path is not used: it
 * requires 4+N (RGBA + params) programs. Rasters are bounded to 320x180. */
constexpr size_t kCreativeMax = 320u * 180u;

size_t creative_parse_bindings(const char *source, char names[][64], size_t capacity) {
    if (!source || !names || !capacity) {
        return 0;
    }
    const char *kernel = strstr(source, "defkernel");
    if (!kernel) {
        return 0;
    }
    const char *open = strchr(kernel, '[');
    const char *close = open ? strchr(open, ']') : nullptr;
    if (!open || !close || close < open) {
        return 0;
    }
    size_t count = 0;
    const char *cursor = open + 1;
    while (cursor < close && count < capacity) {
        while (cursor < close && (*cursor == ' ' || *cursor == '\t' || *cursor == '\n' ||
               *cursor == '\r')) {
            ++cursor;
        }
        if (cursor >= close) {
            break;
        }
        const char *end = cursor;
        while (end < close && *end != ' ' && *end != '\t' && *end != '\n' && *end != '\r') {
            ++end;
        }
        size_t len = (size_t)(end - cursor);
        if (len && len < 64) {
            memcpy(names[count], cursor, len);
            names[count][len] = 0;
            ++count;
        }
        cursor = end;
    }
    return count;
}

void creative_set_error(jfx_desktop_frontend_t *f, const char *message) {
    if (!f) {
        return;
    }
    std::snprintf(f->creative_error, sizeof(f->creative_error), "%s",
        message && message[0] ? message : "creative sketch failed");
}

jfx_result_t creative_compile(jfx_desktop_frontend_t *f, jolt_program_t **out_program,
    char bindings[][64], size_t *out_count) {
    if (out_program) {
        *out_program = nullptr;
    }
    size_t names = creative_parse_bindings(f->creative_source, bindings, 32);
    if (out_count) {
        *out_count = names;
    }
    jolt_program_t *program = nullptr;
    jolt_diagnostic_t diagnostic{};
    diagnostic.size = sizeof(diagnostic);
    if (jolt_compile(f->creative_source, &program, &diagnostic) != JOLT_OK) {
        char formatted[512];
        jolt_diagnostic_format(&diagnostic, "sketch", formatted, sizeof(formatted));
        creative_set_error(f, formatted);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!program) {
        creative_set_error(f, "sketch: compilation produced no program");
        return JFX_ERROR_BACKEND_FAILURE;
    }
    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    if (!data || size < JOLT_BYTECODE_HEADER_SIZE) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: invalid program");
        return JFX_ERROR_BACKEND_FAILURE;
    }
    uint32_t declared = 0;
    memcpy(&declared, data + 8, 4);
    if (declared != names) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: input list changed during compilation");
        return JFX_ERROR_BACKEND_FAILURE;
    }
    if (out_program) {
        *out_program = program;
    } else {
        jolt_program_destroy(program);
    }
    return JFX_SUCCESS;
}

#include "creative_panel.inc"

void preview_panel(jfx_desktop_frontend_t *frontend) {
    ImGui::TextUnformatted("Preview");
    ImGui::Text("Backend: %s",jfx_engine_backend_name(frontend->engine));
    if (frontend->preview_dirty || frontend->playing) {
        if (jfx_desktop_frontend_render_rgba8(frontend,kPreviewWidth,kPreviewHeight,
            frontend->preview_rgba,(size_t)kPreviewWidth*kPreviewHeight*4u)==JFX_SUCCESS) frontend->preview_dirty=false;
    }
    if (frontend->window) {
        void *texture=jfx_desktop_window_upload_rgba8(frontend->window,kPreviewWidth,kPreviewHeight,frontend->preview_rgba);
        if (texture) {
            frontend->preview_texture=texture;
            float width=std::fmin((float)kPreviewWidth,ImGui::GetContentRegionAvail().x);
            const float zoom = (float)frontend->workspace_zoom;
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)),ImVec2(width*zoom,width*9/16*zoom));
        } else ImGui::TextDisabled("Preview texture upload failed.");
    } else ImGui::TextWrapped("Headless: %ux%u preview rendered through %s.",kPreviewWidth,kPreviewHeight,jfx_engine_backend_name(frontend->engine));
    ImGui::TextWrapped("Project: %s",frontend->project_path[0]?frontend->project_path:"Untitled");
    ImGui::TextWrapped("%s",frontend->status);
}

void console_panel() {
    std::lock_guard<std::mutex> guard(g_console.mutex);
    if (ImGui::Button("Clear")) { g_console.next=0; g_console.total=0; }
    ImGui::SameLine(); ImGui::TextDisabled("%u lines",g_console.total); ImGui::Separator();
    uint32_t count=std::min(g_console.total,(uint32_t)kConsoleLines);
    uint32_t first=(g_console.next+(uint32_t)kConsoleLines-count)%(uint32_t)kConsoleLines;
    for (uint32_t i=0;i<count;++i) {
        uint32_t slot=(first+i)%(uint32_t)kConsoleLines;
        auto level=(tilly_log_level_t)g_console.level[slot];
        ImVec4 color(.85f,.85f,.85f,1);
        if (level>=TILLY_LOG_ERROR) color=ImVec4(1,.45f,.45f,1);
        else if (level==TILLY_LOG_WARN) color=ImVec4(1,.8f,.4f,1);
        else if (level<=TILLY_LOG_DEBUG) color=ImVec4(.6f,.65f,.75f,1);
        ImGui::TextColored(color,"%s",g_console.lines[slot]);
    }
}

void statistics_panel(jfx_desktop_frontend_t *frontend) {
    jfx_engine_metrics_t metrics{}; metrics.size=sizeof(metrics);
    jfx_engine_get_metrics(frontend->engine,&metrics);
    ImGui::Text("Engine frames: %llu",(unsigned long long)metrics.frame_count);
    ImGui::Text("Last frame: %.3f ms",(double)metrics.last_frame_ns/1.e6);
    ImGui::Text("Worst frame: %.3f ms",(double)metrics.max_frame_ns/1.e6);
    ImGui::Text("UI frames: %llu",(unsigned long long)frontend->frame_count);
    ImGui::Text("Live buffers/textures/kernels: %u/%u/%u",jfx_engine_live_buffers(frontend->engine),
        jfx_engine_live_textures(frontend->engine),jfx_engine_live_kernels(frontend->engine));
}
void plugin_panel(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("Native plugin SDK 1.0 / effects / Joltscript kernels / editor actions / events");
    ImGui::SetNextItemWidth(-1); ImGui::InputText("##Module path",f->plugin_path,sizeof(f->plugin_path));
    if (ImGui::Button("Load plugin")) { uint32_t id=0; jfx_desktop_frontend_load_plugin(f,f->plugin_path,&id); }
    ImGui::SameLine();
    if (ImGui::Button("Clear editor history")) { finish_grading(f); jfx_editor_clear_history(f->editor); }
    ImGui::Separator();
    for (uint32_t i=0;i<jfx_plugin_host_count(f->plugins);++i) {
        jfx_plugin_info_t info{}; info.size=sizeof(info); uint32_t id;
        if (jfx_plugin_host_info_at(f->plugins,i,&id,&info)!=JFX_SUCCESS) continue;
        ImGui::PushID((int)id);
        ImGui::Text("%s  %u.%u.%u",info.display_name,info.version>>24,(info.version>>12)&4095u,info.version&4095u);
        ImGui::TextDisabled("%s / %s",info.identifier,info.vendor);
        bool removed=ImGui::Button("Unload") && jfx_desktop_frontend_unload_plugin(f,id)==JFX_SUCCESS;
        ImGui::PopID(); if (removed) break;
    }
    ImGui::Separator(); ImGui::TextUnformatted("Editor actions (selected clip/node)");
    for (uint32_t i=0;i<jfx_plugin_host_action_count(f->plugins);++i) {
        jfx_plugin_action_info_t action{}; action.size=sizeof(action);
        if (jfx_plugin_host_action_info(f->plugins,i,&action)!=JFX_SUCCESS) continue;
        ImGui::PushID(action.name);
        if (ImGui::Button(action.label)) jfx_desktop_frontend_invoke_plugin(f,action.name);
        ImGui::PopID();
    }
    ImGui::TextWrapped("%s",f->status);
}

/* Composes the menu bar and every panel. Called between NewFrame and Render. */
void compose_ui(jfx_desktop_frontend_t *frontend) {
    bool open_requested=false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            bool drawing=frontend->workspace==JFX_DESKTOP_WORKSPACE_ANIMATION;
            if (ImGui::MenuItem(drawing?"Open drawing...":"Open Project...")) {
                if(drawing) frontend->drawing->files_requested=true; else open_requested=true;
            }
            if (ImGui::MenuItem("New sequence")) { finish_grading(frontend); jfx_desktop_frontend_close_project(frontend); jfx_desktop_frontend_set_workspace(frontend,JFX_DESKTOP_WORKSPACE_NLE); }
            if (ImGui::MenuItem("New composition")) { finish_grading(frontend); jfx_desktop_frontend_node_compositing_new_graph(frontend); jfx_desktop_frontend_set_workspace(frontend,JFX_DESKTOP_WORKSPACE_COMPOSITING); }
            if (ImGui::MenuItem(drawing?"Save drawing...":"Save project...")) {
                if(drawing) frontend->drawing->files_requested=true; else open_requested=true;
            }
            if (ImGui::MenuItem(drawing?"Export drawing SVG...":"Export Frame...")) {
                if(drawing) frontend->drawing->files_requested=true;
                else {
                auto *t=jfx_editor_timeline(frontend->editor);
                editor_result(frontend,jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_GRAPH?
                    jfx_desktop_frontend_write_graph(frontend,UINT32_MAX,frontend->time_seconds,frontend->node_export_path):
                    jfx_editor_write_frame(frontend->editor,jfx_desktop_frontend_timeline_current_frame(frontend),
                        jfx_timeline_width(t),jfx_timeline_height(t),frontend->nle_export_path));
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                frontend->quit_requested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            if (ImGui::MenuItem("Play", "Space", frontend->playing)) {
                if (frontend->playing) jfx_desktop_frontend_pause(frontend); else jfx_desktop_frontend_play(frontend);
            }
            if (ImGui::MenuItem("Stop", "S")) {
                jfx_desktop_frontend_pause(frontend); jfx_desktop_frontend_seek(frontend,0);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Viewport", nullptr, &frontend->show_viewport);
            ImGui::MenuItem("Timeline", nullptr, &frontend->show_timeline);
            ImGui::MenuItem("Layer Effects", nullptr, &frontend->show_properties);
            ImGui::MenuItem("Color Grading", nullptr, &frontend->show_grade);
            ImGui::MenuItem("Color Calibration", nullptr, &frontend->show_calibration);
            ImGui::MenuItem("Node Compositing", nullptr, &frontend->show_nodes);
            ImGui::MenuItem("Console", nullptr, &frontend->show_console);
            ImGui::MenuItem("Statistics", nullptr, &frontend->show_stats);
            ImGui::MenuItem("Plugins", nullptr, &frontend->show_plugins);
            ImGui::MenuItem("Audio Mixing", nullptr, &frontend->show_audio);
            ImGui::MenuItem("DAW", nullptr, &frontend->show_daw);
            ImGui::MenuItem("2D Animation", nullptr, &frontend->show_animation);
            ImGui::MenuItem("3D Modeling & Animation", nullptr, &frontend->show_modeling3d);
            ImGui::MenuItem("Creative Programming", nullptr, &frontend->show_creative);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Extensions")) {
            if (ImGui::MenuItem("Plugin manager")) jfx_desktop_frontend_set_workspace(frontend,JFX_DESKTOP_WORKSPACE_PLUGINS);
            for (uint32_t i=0;i<jfx_plugin_host_action_count(frontend->plugins);++i) {
                jfx_plugin_action_info_t action{}; action.size=sizeof(action);
                if (jfx_plugin_host_action_info(frontend->plugins,i,&action)!=JFX_SUCCESS) continue;
                ImGui::PushID(action.name);
                if (ImGui::MenuItem(action.label)) jfx_desktop_frontend_invoke_plugin(frontend,action.name);
                ImGui::PopID();
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (open_requested) ImGui::OpenPopup("Open Project");
    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Path to a .jfx project or .jolt kernel:");
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##project", frontend->project_input,
                sizeof(frontend->project_input))) {
            frontend->preview_dirty = true;
        }
        if (ImGui::Button("Open", ImVec2(120, 0))) {
            if (jfx_desktop_frontend_open_project(frontend, frontend->project_input) ==
                JFX_SUCCESS) {
                ImGui::CloseCurrentPopup();
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "invalid path");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Save .jfx", ImVec2(120,0))) {
            auto result=jfx_desktop_frontend_save_project(frontend,frontend->project_input);
            if (result==JFX_SUCCESS) ImGui::CloseCurrentPopup();
            else std::snprintf(frontend->status,sizeof(frontend->status),"%s",jfx_result_to_string(result));
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    auto *viewport=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::Begin("JoltFX workspace",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
        ImGui::GetIO().KeyCtrl && std::fabs(ImGui::GetIO().MouseWheel) > 0.0f) {
        const double next = frontend->workspace_zoom * std::exp((double)ImGui::GetIO().MouseWheel * 0.12);
        jfx_desktop_frontend_set_zoom(frontend, next);
    }
    if (frontend->workspace!=JFX_DESKTOP_WORKSPACE_ANIMATION && frontend->workspace!=JFX_DESKTOP_WORKSPACE_MODELING3D && frontend->workspace!=JFX_DESKTOP_WORKSPACE_CREATIVE) {
    if (ImGui::Button(frontend->playing?"Pause":"Play")) {
        if (frontend->playing) jfx_desktop_frontend_pause(frontend); else jfx_desktop_frontend_play(frontend);
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) { jfx_desktop_frontend_pause(frontend); jfx_desktop_frontend_seek(frontend,0); }
    ImGui::SameLine(); ImGui::SetNextItemWidth(210);
    float time=(float)frontend->time_seconds;
    if (ImGui::SliderFloat("Time",&time,0,(float)frontend->duration_seconds,"%.3f s")) jfx_desktop_frontend_seek(frontend,time);
    ImGui::SameLine();
    float zoom = (float)frontend->workspace_zoom;
    ImGui::SetNextItemWidth(120);
    if (ImGui::SliderFloat("Zoom", &zoom, 0.25f, 4.0f, "%.2fx"))
        jfx_desktop_frontend_set_zoom(frontend, zoom);
    ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_undo(frontend->editor));
    if (ImGui::Button("Undo")) { finish_grading(frontend); editor_result(frontend,jfx_desktop_frontend_edit(frontend,"undo",0,0,0,0,"")); }
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(!jfx_editor_can_redo(frontend->editor));
    if (ImGui::Button("Redo")) { finish_grading(frontend); editor_result(frontend,jfx_desktop_frontend_edit(frontend,"redo",0,0,0,0,"")); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Export...")) { finish_grading(frontend); ImGui::OpenPopup("Encoded export"); }
    if (ImGui::BeginPopupModal("Encoded export",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        encoded_export(frontend);
        ImGui::TextWrapped("%s",frontend->status);
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    auto *t=jfx_editor_timeline(frontend->editor);
    const char *selected=jfx_timeline_clip_name(t,frontend->selected_track,frontend->selected_clip);
    ImGui::SetNextItemWidth(300);
    if (ImGui::BeginCombo("Selected clip",selected?selected:"No clip")) {
        for (uint32_t track=0;track<jfx_timeline_track_count(t);++track)
            for (uint32_t clip=0;clip<jfx_timeline_clip_count(t,track);++clip) {
                char label[256]; std::snprintf(label,sizeof(label),"%s / %s##%u.%u",jfx_timeline_track_name(t,track),jfx_timeline_clip_name(t,track,clip),track,clip);
                if (ImGui::Selectable(label,track==frontend->selected_track && clip==frontend->selected_clip)) {
                    finish_grading(frontend); frontend->selected_track=track; frontend->selected_clip=clip;
                }
            }
        ImGui::EndCombo();
    }
    if (frontend->export_job && jfx_export_state(frontend->export_job)==JFX_EXPORT_RUNNING) {
        ImGui::SetNextItemWidth(260);
        ImGui::ProgressBar((float)((double)jfx_export_completed_frames(frontend->export_job)/(double)jfx_export_total_frames(frontend->export_job)));
        ImGui::SameLine();
        if (ImGui::Button("Cancel export")) jfx_export_cancel(frontend->export_job);
    } else ImGui::TextDisabled("%s",frontend->status);
    } else ImGui::TextDisabled("%s | %s",frontend->workspace==JFX_DESKTOP_WORKSPACE_MODELING3D?"3D scene":frontend->workspace==JFX_DESKTOP_WORKSPACE_CREATIVE?"Creative sketch":"Vector animation",frontend->status);
    ImGui::Separator();
    if (ImGui::BeginTabBar("Interfaces",ImGuiTabBarFlags_FittingPolicyScroll)) {
        for (int i=0;i<JFX_DESKTOP_WORKSPACE_COUNT;++i) {
            if (!jfx_desktop_frontend_panel_visible(frontend,workspace_panels[i])) continue;
            ImGuiTabItemFlags flags=frontend->workspace_requested && (int)frontend->workspace==i?ImGuiTabItemFlags_SetSelected:0;
            if (ImGui::BeginTabItem(workspace_names[i],nullptr,flags)) {
                if ((int)frontend->workspace==i) frontend->workspace_requested=false;
                else if (!frontend->workspace_requested) {
                    jfx_desktop_frontend_set_workspace(frontend,(jfx_desktop_workspace_t)i);
                    frontend->workspace_requested=false;
                }
                bool wide=ImGui::GetContentRegionAvail().x>=760;
                bool animation=i==JFX_DESKTOP_WORKSPACE_ANIMATION || i==JFX_DESKTOP_WORKSPACE_MODELING3D || i==JFX_DESKTOP_WORKSPACE_CREATIVE;
                bool columns=!animation && frontend->show_viewport && wide && ImGui::BeginTable("Workspace layout",2,ImGuiTableFlags_Resizable);
                if (columns) {
                    ImGui::TableSetupColumn("Editor",ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthFixed,330);
                    ImGui::TableNextColumn();
                }
                if (!animation && frontend->show_viewport && !wide && ImGui::CollapsingHeader("Shared preview")) preview_panel(frontend);
                if (ImGui::BeginChild("Editor interface",ImVec2(0,0))) {
                    switch ((jfx_desktop_workspace_t)i) {
                    case JFX_DESKTOP_WORKSPACE_NLE: timeline_tracks(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_EFFECTS: effect_stack(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_CALIBRATION: effect_stack(frontend,JFX_COLOR_CALIBRATION); break;
                    case JFX_DESKTOP_WORKSPACE_GRADING:
                        ImGui::TextUnformatted("Primaries / color wheels / looks / LUTs");
                        effect_stack(frontend,JFX_COLOR_GRADING); break;
                    case JFX_DESKTOP_WORKSPACE_COMPOSITING: node_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_CONSOLE: console_panel(); break;
                    case JFX_DESKTOP_WORKSPACE_STATISTICS: statistics_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_PLUGINS: plugin_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_AUDIO: audio_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_DAW: daw_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_ANIMATION: animation_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_MODELING3D: modeling3d_panel(frontend); break;
                    case JFX_DESKTOP_WORKSPACE_CREATIVE: creative_panel(frontend); break;
                    default: break;
                    }
                }
                ImGui::EndChild();
                if (columns) {
                    ImGui::TableNextColumn();
                    if (ImGui::BeginChild("Shared preview",ImVec2(0,0))) preview_panel(frontend);
                    ImGui::EndChild(); ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
        const auto &io=ImGui::GetIO();
        if (frontend->workspace!=JFX_DESKTOP_WORKSPACE_ANIMATION && (io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_Z)) {
            finish_grading(frontend);
            editor_result(frontend,jfx_desktop_frontend_edit(frontend,io.KeyShift?"redo":"undo",0,0,0,0,""));
            frontend->nle_drag=0;
        }
    }
    if ((frontend->grading_edit || frontend->audio_edit) && !ImGui::IsAnyItemActive()) finish_grading(frontend);
    ImGui::End();
}

} // namespace

extern "C" jfx_result_t jfx_desktop_frontend_draw(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    ImGui::SetCurrentContext(frontend->imgui);

    bool window_open = true;
    if (frontend->window) {
        /* Pumps SDL, feeds ImGui, sets DisplaySize/DeltaTime and opens a frame. */
        window_open = jfx_desktop_window_begin_frame(frontend->window);
    } else {
        /* No window: drive the frame from the configured size and a nominal
         * frame time so the same UI code runs headless. With no renderer
         * backend there is nothing to upload the font atlas, so build it here. */
        ImGuiIO &io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)frontend->width, (float)frontend->height);
        io.DeltaTime = 1.0f / 60.0f;
        if (!io.Fonts->IsBuilt()) {
            io.Fonts->Build();
        }
        ImGui::NewFrame();
    }

    if (window_open) {
        jfx_vst3_pump();
        if (frontend->native_dirty || frontend->native_edit) {
            auto r=native_sync(frontend,false); if (r!=JFX_SUCCESS) editor_result(frontend,r);
        }
        if (frontend->native_session && !jfx_desktop_window_plugin_visible(frontend->window)) {
            auto r=native_sync(frontend,true); if (r!=JFX_SUCCESS) editor_result(frontend,r);
            frontend->native_session=false; jfx_vst3_set_edit_callback(frontend->vst3_inspector,nullptr,nullptr);
        }
        recording_poll(frontend);
        if (frontend->editing && jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_SCENE3D)
            frontend->duration_seconds=(double)jfx_scene3d_frames(jfx_editor_scene3d(frontend->editor))/jfx_scene3d_fps(jfx_editor_scene3d(frontend->editor));
        /* Advance the engine clock while playing, looping at the end of the
         * range so a short project keeps animating. */
        if (frontend->playing) {
            const ImGuiIO &io = ImGui::GetIO();
            const double delta = io.DeltaTime > 0.0f ? (double)io.DeltaTime : 1.0 / 60.0;
            frontend->time_seconds += delta;
            jfx_animation_tab_tick(frontend->animation_tab, delta);
            if (frontend->time_seconds > frontend->duration_seconds) {
                frontend->time_seconds = frontend->looping ? 0.0 : frontend->duration_seconds;
                if (!frontend->looping) frontend->playing = false;
            }
            frontend->preview_dirty = true;
        }
        if (frontend->creative_playing &&
            frontend->workspace == JFX_DESKTOP_WORKSPACE_CREATIVE) {
            const ImGuiIO &cio = ImGui::GetIO();
            const double cdelta = cio.DeltaTime > 0.0f ? (double)cio.DeltaTime : 1.0 / 60.0;
            frontend->creative_t = std::fmod(frontend->creative_t + (float)cdelta, 60.0f);
        }

        if (frontend->editing && jfx_editor_kind(frontend->editor) == JFX_PROJECT_KIND_SEQUENCE)
            frontend->duration_seconds = (double)jfx_timeline_duration(jfx_editor_timeline(frontend->editor)) / jfx_timeline_fps(jfx_editor_timeline(frontend->editor));
        else if (frontend->editing && jfx_editor_kind(frontend->editor)!=JFX_PROJECT_KIND_SCENE3D) frontend->duration_seconds=JFX_DESKTOP_DEFAULT_DURATION;
        compose_ui(frontend);
        if (frontend->export_job && jfx_export_state(frontend->export_job)==JFX_EXPORT_RUNNING) {
            auto r=jfx_export_step(frontend->export_job,1);
            std::snprintf(frontend->status,sizeof(frontend->status),r==JFX_SUCCESS?"Video export: %llu / %llu frames":"Video export failed: %llu / %llu frames",
                (unsigned long long)jfx_export_completed_frames(frontend->export_job),(unsigned long long)jfx_export_total_frames(frontend->export_job));
        }
        if (frontend->export_job && jfx_export_state(frontend->export_job)!=JFX_EXPORT_RUNNING) {
            auto state=jfx_export_state(frontend->export_job);
            std::snprintf(frontend->status,sizeof(frontend->status),"Video export %s: %llu / %llu frames",
                state==JFX_EXPORT_COMPLETE?"complete":state==JFX_EXPORT_CANCELLED?"cancelled":"failed",
                (unsigned long long)jfx_export_completed_frames(frontend->export_job),(unsigned long long)jfx_export_total_frames(frontend->export_job));
            jfx_export_destroy(frontend->export_job); frontend->export_job=nullptr;
        }
        if (!frontend->playing || !frontend->editing || jfx_editor_kind(frontend->editor)!=JFX_PROJECT_KIND_SEQUENCE) reset_audio(frontend);
        else if (frontend->window) {
            if (frontend->audio_mixer && std::fabs((double)frontend->audio_sample/48000.0-frontend->time_seconds)>0.4) reset_audio(frontend);
            if (!frontend->audio_mixer) {
                frontend->audio_sample=(uint64_t)(frontend->time_seconds*48000);
                auto r=jfx_desktop_frontend_audio_mixer(frontend,48000,&frontend->audio_mixer);
                if (r!=JFX_SUCCESS) { frontend->playing=false; editor_result(frontend,r); }
            }
            if (frontend->audio_mixer && jfx_desktop_window_queued_audio(frontend->window)<48000u*8u/5u) {
                float pcm[4096*2];
                auto r=jfx_audio_mixer_render(frontend->audio_mixer,frontend->audio_sample,4096,pcm,8192);
                if (r!=JFX_SUCCESS) { frontend->playing=false; reset_audio(frontend); editor_result(frontend,r); }
                else if (jfx_desktop_window_queue_audio(frontend->window,pcm,4096)) {
                    frontend->audio_peak[0]=frontend->audio_peak[1]=0;
                    for (size_t sample=0;sample<4096;++sample)
                        for (size_t channel=0;channel<2;++channel)
                            frontend->audio_peak[channel]=std::fmax(frontend->audio_peak[channel],std::fabs(pcm[sample*2+channel]));
                    frontend->audio_sample+=4096;
                }
                else { frontend->playing=false; reset_audio(frontend); std::snprintf(frontend->status,sizeof(frontend->status),"Unable to open or queue the audio output device."); }
            }
        }
        frontend->layout_initialized = true;

        if (frontend->window) {
            jfx_desktop_window_end_frame(frontend->window);
        } else {
            ImGui::Render();
        }
        frontend->frame_count++;
        return jfx_engine_tick(frontend->engine);
    }

    /* The window was closed by the user: close out the ImGui frame cleanly so
     * the backends stay balanced, then let the runner stop. */
    ImGui::Render();
    frontend->frame_count++;
    return JFX_SUCCESS;
}

extern "C" bool jfx_desktop_frontend_should_quit(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->quit_requested : true;
}

extern "C" jfx_result_t jfx_desktop_frontend_run(jfx_desktop_frontend_t *frontend,
    uint64_t max_frames, double max_seconds) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    if (!jfx_desktop_window_available()) {
        std::fprintf(stderr,
            "no windowing backend in this build; run with --headless-smoke instead\n");
        return JFX_ERROR_NOT_INITIALIZED;
    }

    jfx_desktop_window_config_t window_config;
    window_config.width = frontend->width;
    window_config.height = frontend->height;
    window_config.title = "JoltFX";
    window_config.resizable = true;
    char error[256] = { 0 };
    frontend->window = jfx_desktop_window_create(&window_config, error, sizeof(error));
    if (!frontend->window) {
        std::fprintf(stderr, "failed to open a window: %s\n", error);
        return JFX_ERROR_NOT_INITIALIZED;
    }

    ImGui::SetCurrentContext(frontend->imgui);
    std::snprintf(frontend->status, sizeof(frontend->status), "window open (%s)",
        jfx_desktop_window_backend_name());
    frontend->preview_dirty = true;
    frontend->quit_requested = false;
    /* Apply the default panel geometry once the real surface size is known. */
    frontend->layout_initialized = false;

    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    const uint64_t first_frame = frontend->frame_count;
    while (max_frames == 0 || frontend->frame_count - first_frame < max_frames) {
        if (jfx_desktop_window_drawable_width(frontend->window)) {
            frontend->width = jfx_desktop_window_drawable_width(frontend->window);
            frontend->height = jfx_desktop_window_drawable_height(frontend->window);
        }
        const jfx_result_t status = jfx_desktop_frontend_draw(frontend);
        if (status != JFX_SUCCESS) {
            return status;
        }
        if (frontend->quit_requested) {
            break;
        }
        if (max_seconds > 0.0) {
            const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - started;
            if (elapsed.count() >= max_seconds) {
                break;
            }
        }
    }
    return JFX_SUCCESS;
}

extern "C" const char *jfx_desktop_frontend_project_path(const jfx_desktop_frontend_t *frontend) {
    return frontend && frontend->project_path[0] ? frontend->project_path : nullptr;
}

extern "C" const char *jfx_desktop_frontend_backend_name(const jfx_desktop_frontend_t *frontend) {
    return frontend ? jfx_engine_backend_name(frontend->engine) : nullptr;
}

extern "C" const char *jfx_desktop_frontend_effect_name(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->effect_name : nullptr;
}

extern "C" float jfx_desktop_frontend_effect_parameter(
    const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->effect_parameter : 0.0f;
}

extern "C" double jfx_desktop_frontend_time(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->time_seconds : 0.0;
}

extern "C" bool jfx_desktop_frontend_playing(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->playing : false;
}

extern "C" uint32_t jfx_desktop_frontend_width(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->width : 0u;
}

extern "C" uint32_t jfx_desktop_frontend_height(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->height : 0u;
}

extern "C" uint64_t jfx_desktop_frontend_frame_count(const jfx_desktop_frontend_t *frontend) {
    return frontend ? frontend->frame_count : 0u;
}

/* Public panel operations delegate to the shared editor models. */
static jfx_result_t edited(jfx_desktop_frontend_t *f, jfx_result_t r) {
    if (f && r == JFX_SUCCESS) { reset_audio(f); f->editing = true; f->preview_dirty = true; }
    return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_edit(jfx_desktop_frontend_t *f,const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *text) {
    if (!f || !op) return JFX_ERROR_INVALID_ARGUMENT;
    if (f->recording) return JFX_ERROR_BUSY;
    if (f->scene_navigation && std::strcmp(op,"3d.navigation_end") && std::strcmp(op,"3d.navigation_cancel") &&
        std::strcmp(op,"3d.orbit") && std::strcmp(op,"3d.orbit_axis") && std::strcmp(op,"3d.pan") && std::strcmp(op,"3d.dolly")) {
        auto r=jfx_editor_command(f->editor,"3d.navigation_end",0,0,0,0,""); if (r!=JFX_SUCCESS) return r; f->scene_navigation=false;
    }
    if (f->native_session) {
        auto synced=native_sync(f,true); if (synced!=JFX_SUCCESS) return synced;
        close_inspector(f);
    }
    if (!std::strcmp(op,"undo") || !std::strcmp(op,"redo") || !std::strcmp(op,"sequence.new") ||
        !std::strcmp(op,"audio.insert.remove") || !std::strcmp(op,"audio.insert.move") || !std::strcmp(op,"track.remove") || !std::strcmp(op,"track.move")) close_inspector(f);
    if (f->audio_edit) finish_audio(f);
    if (f->grading_edit && std::strcmp(op,"effect.param")) finish_grading(f);
    auto r=edited(f,jfx_editor_command(f->editor,op,a,b,c,v,text));
    if (r==JFX_SUCCESS && (!std::strcmp(op,"graph.new") || !std::strcmp(op,"sequence.new"))) {
        f->time_seconds=0; f->playing=false; f->selected_node=0; f->node_preview_selected=false; f->node_drag=0; f->node_wiring=false;
    }
    if (r==JFX_SUCCESS && (!std::strcmp(op,"undo") || !std::strcmp(op,"redo") || !std::strcmp(op,"node.remove"))) {
        f->node_preview_selected=false; f->node_drag=0; f->node_wiring=false;
        if (f->selected_node>=jfx_graph_node_count(jfx_editor_graph(f->editor))) f->selected_node=0;
    }
    if (r==JFX_SUCCESS && (!std::strcmp(op,"undo") || !std::strcmp(op,"redo")) &&
        (f->workspace<=JFX_DESKTOP_WORKSPACE_COMPOSITING || f->workspace==JFX_DESKTOP_WORKSPACE_AUDIO || f->workspace==JFX_DESKTOP_WORKSPACE_DAW) &&
        (f->workspace==JFX_DESKTOP_WORKSPACE_COMPOSITING)!=(jfx_editor_kind(f->editor)==JFX_PROJECT_KIND_GRAPH))
        jfx_desktop_frontend_set_workspace(f,jfx_editor_kind(f->editor)==JFX_PROJECT_KIND_GRAPH?JFX_DESKTOP_WORKSPACE_COMPOSITING:JFX_DESKTOP_WORKSPACE_NLE);
    return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_sequence_state(jfx_desktop_frontend_t *f,char *out,size_t cap) {
    return f?jfx_editor_sequence_state(f->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_scene3d_state(jfx_desktop_frontend_t *f,char *out,size_t cap) {
    return f?jfx_editor_scene3d_state(f->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_write_frame(jfx_desktop_frontend_t *f,uint64_t frame,const char *path) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    auto *t=jfx_editor_timeline(f->editor);
    return jfx_editor_write_frame(f->editor,frame,jfx_timeline_width(t),jfx_timeline_height(t),path);
}
extern "C" jfx_result_t jfx_desktop_frontend_export_begin(jfx_desktop_frontend_t *f,const jfx_export_options_t *o,jfx_export_job_t **out) {
    if (f) { auto r=native_sync(f,true); if (r!=JFX_SUCCESS) return r; }
    return jfx_export_begin(f?f->editor:nullptr,o,out);
}
extern "C" jfx_result_t jfx_desktop_frontend_audio_mixer(jfx_desktop_frontend_t *f,uint32_t rate,jfx_audio_mixer_t **out) {
    return jfx_audio_mixer_create(f?jfx_editor_timeline(f->editor):nullptr,rate,out);
}
extern "C" jfx_result_t jfx_desktop_frontend_set_panel_visible(jfx_desktop_frontend_t *f, jfx_desktop_panel_t p, bool v) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    bool *panels[] = { &f->show_viewport, &f->show_timeline, &f->show_properties, &f->show_grade, &f->show_nodes, &f->show_console, &f->show_stats, &f->show_calibration, &f->show_plugins, &f->show_audio, &f->show_daw, &f->show_animation, &f->show_modeling3d, &f->show_creative };
    if ((unsigned)p >= JFX_DESKTOP_PANEL_COUNT) return JFX_ERROR_INVALID_ARGUMENT;
    *panels[p] = v; return JFX_SUCCESS;
}
extern "C" bool jfx_desktop_frontend_panel_visible(const jfx_desktop_frontend_t *f, jfx_desktop_panel_t p) {
    if (!f || (unsigned)p >= JFX_DESKTOP_PANEL_COUNT) return false;
    const bool panels[] = { f->show_viewport, f->show_timeline, f->show_properties, f->show_grade, f->show_nodes, f->show_console, f->show_stats, f->show_calibration, f->show_plugins, f->show_audio, f->show_daw, f->show_animation, f->show_modeling3d, f->show_creative };
    return panels[p];
}
extern "C" jfx_result_t jfx_desktop_frontend_set_workspace(jfx_desktop_frontend_t *f,jfx_desktop_workspace_t workspace) {
    if (!f || (unsigned)workspace>=JFX_DESKTOP_WORKSPACE_COUNT) return JFX_ERROR_INVALID_ARGUMENT;
    finish_grading(f);
    finish_audio(f);
    if (f->scene_navigation) {
        auto r=jfx_editor_command(f->editor,"3d.navigation_end",0,0,0,0,"");
        if (r!=JFX_SUCCESS) return r;
        f->scene_navigation=false;
    }
    if (f->drawing && f->workspace!=workspace) { f->drawing->dragging=false; f->drawing->playing=false; }
    if (workspace==JFX_DESKTOP_WORKSPACE_ANIMATION) jfx_desktop_frontend_pause(f);
    f->workspace=workspace; f->workspace_requested=true;
    jfx_desktop_frontend_set_panel_visible(f,workspace_panels[workspace],true);
    f->creative_dirty = true;
    f->nle_drag=f->node_drag=0; f->node_wiring=false;
    if (workspace==JFX_DESKTOP_WORKSPACE_MODELING3D) {
        jfx_editor_set_kind(f->editor,JFX_PROJECT_KIND_SCENE3D);
        f->editing=true; f->preview_dirty=true; f->node_preview_selected=false;
        f->duration_seconds=(double)jfx_scene3d_frames(jfx_editor_scene3d(f->editor))/jfx_scene3d_fps(jfx_editor_scene3d(f->editor));
        f->time_seconds=0; f->playing=false; reset_audio(f);
    }
    if (workspace<=JFX_DESKTOP_WORKSPACE_COMPOSITING || workspace==JFX_DESKTOP_WORKSPACE_AUDIO || workspace==JFX_DESKTOP_WORKSPACE_DAW) {
        jfx_editor_set_kind(f->editor,workspace==JFX_DESKTOP_WORKSPACE_COMPOSITING?JFX_PROJECT_KIND_GRAPH:JFX_PROJECT_KIND_SEQUENCE);
        f->editing=true; f->preview_dirty=true; reset_audio(f);
        if (workspace!=JFX_DESKTOP_WORKSPACE_COMPOSITING) f->node_preview_selected=false;
    }
    return JFX_SUCCESS;
}
extern "C" jfx_desktop_workspace_t jfx_desktop_frontend_workspace(const jfx_desktop_frontend_t *f) {
    return f?f->workspace:JFX_DESKTOP_WORKSPACE_NLE;
}
extern "C" jfx_plugin_host_t *jfx_desktop_frontend_plugins(jfx_desktop_frontend_t *f) { return f?f->plugins:nullptr; }
extern "C" jfx_result_t jfx_desktop_frontend_load_plugin(jfx_desktop_frontend_t *f,const char *path,uint32_t *out_id) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    auto r=jfx_plugin_host_load(f->plugins,path,out_id);
    std::snprintf(f->status,sizeof(f->status),"%s",r==JFX_SUCCESS?"Plugin loaded.":jfx_plugin_host_error(f->plugins));
    return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_unload_plugin(jfx_desktop_frontend_t *f,uint32_t id) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    auto r=jfx_plugin_host_unload(f->plugins,id);
    std::snprintf(f->status,sizeof(f->status),"%s",r==JFX_SUCCESS?"Plugin unloaded.":jfx_plugin_host_error(f->plugins));
    return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_invoke_plugin(jfx_desktop_frontend_t *f,const char *action) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    finish_grading(f);
    jfx_plugin_action_context_t context{sizeof(context),f->editor,f->selected_track,f->selected_clip,f->selected_node};
    auto r=jfx_plugin_host_invoke(f->plugins,action,&context);
    if (r==JFX_SUCCESS) { std::snprintf(f->status,sizeof(f->status),"Plugin action completed."); return edited(f,r); }
    std::snprintf(f->status,sizeof(f->status),"%s",jfx_plugin_host_error(f->plugins)); return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_save_project(jfx_desktop_frontend_t *f, const char *path) {
    if (!f || !path || !*path) return JFX_ERROR_INVALID_ARGUMENT;
    auto synced=native_sync(f,true); if (synced!=JFX_SUCCESS) return synced;
    finish_grading(f);
    char *text = static_cast<char *>(imgui_alloc(JFX_PROJECT_MAX_BYTES, nullptr));
    if (!text) return JFX_ERROR_OUT_OF_MEMORY;
    size_t n=0; jfx_result_t r=jfx_editor_save(f->editor,text,JFX_PROJECT_MAX_BYTES,&n);
    if (r == JFX_SUCCESS) {
        FILE *file=std::fopen(path,"wb");
        if (!file) r=JFX_ERROR_NOT_FOUND;
        else { bool ok=std::fwrite(text,1,n,file)==n; if (std::fclose(file)!=0) ok=false; if (!ok) r=JFX_ERROR_INVALID_ARGUMENT; }
    }
    imgui_free(text,nullptr); return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_close_project(jfx_desktop_frontend_t *f) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    if (f->recording) return JFX_ERROR_BUSY;
    auto synced=native_sync(f,true); if (synced!=JFX_SUCCESS) return synced;
    close_inspector(f);
    finish_grading(f);
    jfx_editor_t *e=jfx_editor_create(kPreviewWidth,kPreviewHeight);
    if (!e) return JFX_ERROR_OUT_OF_MEMORY;
    auto r=jfx_editor_command(e,"sequence.new",kPreviewWidth,kPreviewHeight,30,1,"");
    if (r!=JFX_SUCCESS) { jfx_editor_destroy(e); return r; }
    jfx_editor_clear_history(e);
    jfx_editor_destroy(f->editor); f->editor=e; f->editing=true;
    f->project_path[0]=0; f->time_seconds=0; f->playing=false;
    f->selected_track=f->selected_clip=f->selected_node=0;
    f->node_preview_selected=false; f->node_drag=0; f->node_wiring=false;
    return edited(f,JFX_SUCCESS);
}
extern "C" jfx_result_t jfx_desktop_frontend_timeline_play(jfx_desktop_frontend_t *f) { return jfx_desktop_frontend_play(f); }
extern "C" jfx_result_t jfx_desktop_frontend_timeline_pause(jfx_desktop_frontend_t *f) { return jfx_desktop_frontend_pause(f); }
extern "C" jfx_result_t jfx_desktop_frontend_timeline_seek(jfx_desktop_frontend_t *f,double s) { return jfx_desktop_frontend_seek(f,s); }
extern "C" jfx_result_t jfx_desktop_frontend_timeline_set_loop(jfx_desktop_frontend_t *f,bool v) { if (!f) return JFX_ERROR_INVALID_ARGUMENT; f->looping=v; return JFX_SUCCESS; }
extern "C" double jfx_desktop_frontend_timeline_duration(const jfx_desktop_frontend_t *f) { return f ? (double)jfx_timeline_duration(jfx_editor_timeline(f->editor))/jfx_timeline_fps(jfx_editor_timeline(f->editor)) : 0; }
extern "C" uint64_t jfx_desktop_frontend_timeline_current_frame(const jfx_desktop_frontend_t *f) { return f ? (uint64_t)std::floor(f->time_seconds*jfx_timeline_fps(jfx_editor_timeline(f->editor))+1.e-7) : 0; }
extern "C" jfx_result_t jfx_desktop_frontend_timeline_set_current_frame(jfx_desktop_frontend_t *f,uint64_t frame) { return f ? jfx_desktop_frontend_seek(f,(double)frame/jfx_timeline_fps(jfx_editor_timeline(f->editor))) : JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_add(jfx_desktop_frontend_t *f,uint32_t t,uint32_t c,const char *kind) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_desktop_frontend_edit(f,"effect.add",t,c,0,0,kind);
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_remove(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e) {
    return jfx_desktop_frontend_edit(f,"effect.remove",t,c,e,0,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_move(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, uint32_t to_index) {
    return jfx_desktop_frontend_edit(f,"effect.move",t,c,e,to_index,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_set_enabled(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, bool enabled) {
    return jfx_desktop_frontend_edit(f,"effect.enabled",t,c,e,enabled,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_set_opacity(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, float opacity) {
    return jfx_desktop_frontend_edit(f,"effect.opacity",t,c,e,opacity,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_set_blend(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, jfx_blend_mode_t mode) {
    return jfx_desktop_frontend_edit(f,"effect.blend",t,c,e,mode,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_set_param(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, size_t param, float value) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    const auto *kind=jfx_timeline_effect_kind_desc(jfx_editor_timeline(f->editor),t,c,e);
    return kind && param<kind->param_count?jfx_desktop_frontend_edit(f,"effect.param",t,c,e,value,kind->params[param].name):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_set_string(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, size_t index, const char *text) {
    return jfx_desktop_frontend_edit(f,"effect.string",t,c,e,(double)index,text);
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_add_key(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, size_t param, uint64_t frame, float value) {
    char text[96]; std::snprintf(text,sizeof(text),"%zu %llu",param,(unsigned long long)frame);
    return jfx_desktop_frontend_edit(f,"effect.key.add",t,c,e,value,text);
}
extern "C" jfx_result_t jfx_desktop_frontend_layer_effects_remove_key(jfx_desktop_frontend_t *f, uint32_t t, uint32_t c, uint32_t e, size_t param, uint64_t frame) {
    char text[96]; std::snprintf(text,sizeof(text),"%zu %llu",param,(unsigned long long)frame);
    return jfx_desktop_frontend_edit(f,"effect.key.remove",t,c,e,0,text);
}

extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_new_graph(jfx_desktop_frontend_t *f) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_desktop_frontend_edit(f,"graph.new",kPreviewWidth,kPreviewHeight,0,0,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_add_node(jfx_desktop_frontend_t *f,const char *kind,const char *label,uint32_t *out) {
    if (!f || !out || (label && std::strlen(label)>=JFX_NODE_LABEL_MAX)) return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t index=(uint32_t)jfx_graph_node_count(jfx_editor_graph(f->editor));
    auto r=jfx_desktop_frontend_edit(f,"node.add",0,0,0,0,kind);
    if (r==JFX_SUCCESS) {
        if (label) jfx_graph_set_node_label(jfx_editor_graph(f->editor),index,label);
        *out=index;
    }
    return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_remove_node(jfx_desktop_frontend_t *f,uint32_t node) {
    return jfx_desktop_frontend_edit(f,"node.remove",node,0,0,0,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_connect(jfx_desktop_frontend_t *f,uint32_t from,size_t fp,uint32_t to,size_t tp) {
    if (fp>UINT32_MAX || tp>UINT32_MAX) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_desktop_frontend_edit(f,"node.connect",from,to,(uint32_t)tp,(double)fp,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_disconnect(jfx_desktop_frontend_t *f,uint32_t to,size_t tp) {
    return tp>UINT32_MAX?JFX_ERROR_INVALID_ARGUMENT:jfx_desktop_frontend_edit(f,"node.disconnect",to,(uint32_t)tp,0,0,"");
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_set_param(jfx_desktop_frontend_t *f,uint32_t node,size_t p,float v) {
    if (!f || !std::isfinite(v)) return JFX_ERROR_INVALID_ARGUMENT;
    auto *g=jfx_editor_graph(f->editor); const auto *k=jfx_graph_node_kind(g,node);
    if (!k || p>=k->param_count || v<k->params[p].minimum || v>k->params[p].maximum) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_desktop_frontend_edit(f,"node.param",node,0,0,v,k->params[p].name);
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_set_string(jfx_desktop_frontend_t *f,uint32_t node,size_t i,const char *text) {
    return i>UINT32_MAX?JFX_ERROR_INVALID_ARGUMENT:jfx_desktop_frontend_edit(f,"node.path",node,(uint32_t)i,0,0,text);
}
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_set_output(jfx_desktop_frontend_t *f,uint32_t node) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_result_t r=jfx_desktop_frontend_edit(f,"node.output",node,0,0,0,"");
    if (r==JFX_SUCCESS) jfx_editor_set_kind(f->editor,JFX_PROJECT_KIND_GRAPH);
    if (r==JFX_SUCCESS) f->node_preview_selected=false;
    return edited(f,r);
}
extern "C" uint32_t jfx_desktop_frontend_node_compositing_output(const jfx_desktop_frontend_t *f) { return f ? jfx_editor_output(f->editor) : UINT32_MAX; }
extern "C" jfx_result_t jfx_desktop_frontend_node_compositing_render(jfx_desktop_frontend_t *f,uint32_t w,uint32_t h,uint8_t *out,size_t n) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_editor_render_graph(f->editor,UINT32_MAX,f->time_seconds,w,h,out,n);
}
extern "C" jfx_result_t jfx_desktop_frontend_graph_state(jfx_desktop_frontend_t *f,char *out,size_t cap) {
    return f?jfx_editor_graph_state(f->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_render_graph(jfx_desktop_frontend_t *f,uint32_t node,double seconds,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    return f?jfx_editor_render_graph(f->editor,node,seconds,w,h,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_write_graph(jfx_desktop_frontend_t *f,uint32_t node,double seconds,const char *path) {
    return f?jfx_editor_write_graph(f->editor,node,seconds,jfx_editor_graph_width(f->editor),jfx_editor_graph_height(f->editor),path):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_creative_set_source(jfx_desktop_frontend_t *f,
    const char *source) {
    if (!f || !source || !source[0] || std::strlen(source) >= sizeof(f->creative_source)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    std::snprintf(f->creative_source, sizeof(f->creative_source), "%s", source);
    f->creative_error[0] = 0;
    f->creative_status = JFX_SUCCESS;
    f->creative_dirty = true;
    return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_desktop_frontend_creative_source(const jfx_desktop_frontend_t *f,
    char *out, size_t capacity) {
    if (!f || !out || !capacity) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t n = std::strlen(f->creative_source);
    if (n + 1 > capacity) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    std::memcpy(out, f->creative_source, n + 1);
    return JFX_SUCCESS;
}
extern "C" const char *jfx_desktop_frontend_creative_error(const jfx_desktop_frontend_t *f) {
    if (!f || !f->creative_error[0]) {
        return f ? f->creative_error : "";
    }
    return f->creative_error;
}
extern "C" jfx_result_t jfx_desktop_frontend_creative_render(jfx_desktop_frontend_t *f,
    uint32_t width, uint32_t height, uint8_t *out_rgba, size_t out_size) {
    if (!f || !width || !height || width > 320 || height > 180) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > kCreativeMax || pixels > SIZE_MAX / 4u) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    char bindings[32][64];
    size_t binding_count = 0;
    jolt_program_t *program = nullptr;
    jfx_result_t compiled = creative_compile(f, &program, bindings, &binding_count);
    if (compiled != JFX_SUCCESS) {
        return compiled;
    }
    // Validate-only call from the Run button / tests.
    if (!out_rgba) {
        jolt_program_destroy(program);
        f->creative_error[0] = 0;
        f->creative_status = JFX_SUCCESS;
        return JFX_SUCCESS;
    }
    if (out_size < pixels * 4u) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: destination too small");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t program_size = 0;
    const uint8_t *bytecode = jolt_program_data(program, &program_size);
    uint32_t outputs = 0;
    if (bytecode && program_size >= JOLT_BYTECODE_HEADER_SIZE) {
        memcpy(&outputs, bytecode + 12, 4);
    }
    if (!bytecode || (outputs != 1 && outputs != 4)) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: invalid program outputs");
        return JFX_ERROR_BACKEND_FAILURE;
    }
    jolt_vm_t *vm = jolt_vm_create();
    if (!vm) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: cannot create the VM");
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    float args[32];
    float result[4];
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float nx = width > 1 ? (float)x / (float)(width - 1) : 0.0f;
            float ny = height > 1 ? (float)y / (float)(height - 1) : 0.0f;
            for (size_t i = 0; i < binding_count; ++i) {
                if (!strcmp(bindings[i], "x")) {
                    args[i] = nx;
                } else if (!strcmp(bindings[i], "y")) {
                    args[i] = ny;
                } else if (!strcmp(bindings[i], "t")) {
                    args[i] = f->creative_t;
                } else if (!strcmp(bindings[i], "mx")) {
                    args[i] = f->creative_mx;
                } else if (!strcmp(bindings[i], "my")) {
                    args[i] = f->creative_my;
                } else {
                    args[i] = 0.0f;
                }
            }
            if (jolt_vm_run(vm, bytecode, program_size, args, binding_count, result,
                    outputs) != JOLT_OK) {
                jolt_vm_destroy(vm);
                jolt_program_destroy(program);
                creative_set_error(f, "sketch: runtime failure (non-finite input or budget)");
                return JFX_ERROR_BACKEND_FAILURE;
            }
            size_t base = ((size_t)y * width + x) * 4u;
            for (size_t c = 0; c < 4; ++c) {
                float v = outputs == 1 ? (c < 3 ? result[0] : 1.0f) : result[c];
                if (!(v > 0.0f)) {
                    out_rgba[base + c] = 0;
                } else if (v >= 1.0f) {
                    out_rgba[base + c] = 255;
                } else {
                    out_rgba[base + c] = (uint8_t)(v * 255.0f + 0.5f);
                }
            }
        }
    }
    jolt_vm_destroy(vm);
    jolt_program_destroy(program);
    f->creative_error[0] = 0;
    f->creative_status = JFX_SUCCESS;
    f->creative_dirty = false;
    return JFX_SUCCESS;
}
static jfx_result_t creative_write_file(const char *path, const uint8_t *data, size_t size) {
    if (!path || !path[0] || (!data && size)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    FILE *file = std::fopen(path, "wb");
    if (!file) {
        return JFX_ERROR_NOT_FOUND;
    }
    bool ok = size == 0 || std::fwrite(data, 1, size, file) == size;
    if (std::fclose(file) != 0) {
        ok = false;
    }
    return ok ? JFX_SUCCESS : JFX_ERROR_INVALID_ARGUMENT;
}
static jfx_result_t creative_export_object(jfx_desktop_frontend_t *f, const char *path) {
    char bindings[32][64];
    jolt_program_t *program = nullptr;
    jfx_result_t compiled = creative_compile(f, &program, bindings, nullptr);
    if (compiled != JFX_SUCCESS) {
        return compiled;
    }
    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    if (!data || !size) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: nothing to export");
        return JFX_ERROR_BACKEND_FAILURE;
    }
    // Minimal ELF64 relocatable: header + `.jolt` payload section + 3 section
    // headers. Mirrors `zoltan export --as obj` byte for byte in layout.
    static const uint8_t names[] = "\0.shstrtab\0.jolt\0";
    const uint64_t shstr_off = 64;
    const uint64_t jolt_off = shstr_off + sizeof(names);
    uint64_t sh_off = (jolt_off + size + 7) & ~(uint64_t)7;
    size_t total = (size_t)(sh_off + 3 * 64);
    if (total < sh_off) {
        jolt_program_destroy(program);
        creative_set_error(f, "sketch: program too large to wrap");
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    std::vector<uint8_t> elf(total, 0);
    elf[0] = 0x7f;
    elf[1] = 'E';
    elf[2] = 'L';
    elf[3] = 'F';
    elf[4] = 2;
    elf[5] = 1;
    elf[6] = 1;
    elf[16] = 1;
    elf[18] = 62;
    elf[20] = 1;
    memcpy(&elf[40], &sh_off, 8);
    elf[52] = 64;
    elf[58] = 64;
    elf[60] = 3;
    elf[62] = 1;
    memcpy(&elf[shstr_off], names, sizeof(names));
    memcpy(&elf[jolt_off], data, size);
    size_t at = (size_t)sh_off + 64;
    // Section 1: .shstrtab.
    elf[at + 0] = 1;
    elf[at + 4] = 3;
    memcpy(&elf[at + 24], &shstr_off, 8);
    uint64_t names_size = sizeof(names);
    memcpy(&elf[at + 32], &names_size, 8);
    at += 64;
    // Section 2: .jolt.
    elf[at + 0] = 11;
    elf[at + 4] = 1;
    memcpy(&elf[at + 24], &jolt_off, 8);
    uint64_t payload = size;
    memcpy(&elf[at + 32], &payload, 8);
    jfx_result_t written = creative_write_file(path, elf.data(), elf.size());
    jolt_program_destroy(program);
    if (written != JFX_SUCCESS) {
        creative_set_error(f, "sketch: cannot write the object file");
    } else {
        f->creative_error[0] = 0;
    }
    return written;
}
static jfx_result_t creative_export_html(jfx_desktop_frontend_t *f, const char *path) {
    char bindings[32][64];
    size_t binding_count = 0;
    jolt_program_t *program = nullptr;
    jfx_result_t compiled = creative_compile(f, &program, bindings, &binding_count);
    if (compiled != JFX_SUCCESS) {
        return compiled;
    }
    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    // Lower the instruction stream to JS. Only the straight-line ops the CLI
    // exporter supports are lowered; anything else is an honest error.
    std::string js = "(x,y,t,mx,my)=>{const s=[];const inp=[x,y,t,mx,my];";
    js += "while(inp.length<" + std::to_string(binding_count) + ")inp.push(0);";
    size_t offset = JOLT_BYTECODE_HEADER_SIZE;
    jfx_result_t status = JFX_SUCCESS;
    while (offset + 8 <= size) {
        uint32_t op = 0, operand = 0;
        memcpy(&op, data + offset, 4);
        memcpy(&operand, data + offset + 4, 4);
        offset += 8;
        switch (op) {
        case 1: {
            float v = 0;
            memcpy(&v, &operand, 4);
            char literal[64];
            std::snprintf(literal, sizeof(literal), "s.push(%g);", (double)v);
            js += literal;
            break;
        }
        case 2:
            js += "s.push(inp[" + std::to_string(operand) + "]);";
            break;
        case 3:
            js += "s.push(s.pop()+s.pop());";
            break;
        case 4:
            js += "{const b=s.pop(),a=s.pop();s.push(a-b);}";
            break;
        case 5:
            js += "s.push(s.pop()*s.pop());";
            break;
        case 6:
            js += "{const b=s.pop(),a=s.pop();s.push(a/b);}";
            break;
        case 7:
            js += "s.push(Math.min(s.pop(),s.pop()));";
            break;
        case 8:
            js += "s.push(Math.max(s.pop(),s.pop()));";
            break;
        case 9:
            js += "s.push(Math.abs(s.pop()));";
            break;
        case 10:
            js += "s.push(Math.floor(s.pop()));";
            break;
        case 11:
            js += "{const b=s.pop(),a=s.pop();s.push(Math.pow(a,b));}";
            break;
        case 12:
            js += "s.push(Math.sqrt(s.pop()));";
            break;
        case 13:
            js += "{const b=s.pop(),a=s.pop();s.push(a<b?1:0);}";
            break;
        case 14:
            js += "{const c=s.pop(),b=s.pop(),a=s.pop();s.push(a!==0?b:c);}";
            break;
        case 15:
            js += "if(" + std::to_string(operand) + "===0)out0=s.pop();";
            break;
        default:
            status = JFX_ERROR_NOT_IMPLEMENTED;
            break;
        }
        if (status != JFX_SUCCESS) {
            break;
        }
    }
    jolt_program_destroy(program);
    if (status != JFX_SUCCESS) {
        creative_set_error(f, "sketch: HTML export supports the basic scalar ops only");
        return status;
    }
    js += "return out0;}";
    std::string html = "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">"
        "<title>sketch</title></head>\n<body>\n<canvas id=\"c\" width=\"320\" height=\"180\"></canvas>\n"
        "<script>\nconst pixel=" + js +
        "\nconst canvas=document.getElementById('c');\nconst ctx=canvas.getContext('2d');\n"
        "const img=ctx.createImageData(canvas.width,canvas.height);\nlet t=0;\nfunction frame(){\n"
        " for(let y=0;y<canvas.height;++y)for(let x=0;x<canvas.width;++x){\n"
        "  const nx=x/(canvas.width-1),ny=y/(canvas.height-1);\n"
        "  const v=pixel(nx,ny,t,0.5,0.5);\n  const i=(y*canvas.width+x)*4;\n"
        "  img.data[i]=nx*255;img.data[i+1]=ny*255;img.data[i+2]=v*255;img.data[i+3]=255;\n }\n"
        " ctx.putImageData(img,0,0);t+=1/30;requestAnimationFrame(frame);}\nframe();\n</script>\n</body></html>\n";
    jfx_result_t written =
        creative_write_file(path, reinterpret_cast<const uint8_t *>(html.data()), html.size());
    if (written != JFX_SUCCESS) {
        creative_set_error(f, "sketch: cannot write the HTML file");
    } else {
        f->creative_error[0] = 0;
    }
    return written;
}
static jfx_result_t creative_export_wat(jfx_desktop_frontend_t *f, const char *path) {
    char bindings[32][64];
    jolt_program_t *program = nullptr;
    jfx_result_t compiled = creative_compile(f, &program, bindings, nullptr);
    if (compiled != JFX_SUCCESS) {
        return compiled;
    }
    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    std::string wat =
        "(module\n (func $pixel (param $x f32) (param $y f32) (param $t f32) (result f32)\n";
    size_t offset = JOLT_BYTECODE_HEADER_SIZE;
    jfx_result_t status = JFX_SUCCESS;
    while (offset + 8 <= size) {
        uint32_t op = 0, operand = 0;
        memcpy(&op, data + offset, 4);
        memcpy(&operand, data + offset + 4, 4);
        offset += 8;
        switch (op) {
        case 1: {
            float v = 0;
            memcpy(&v, &operand, 4);
            char literal[64];
            std::snprintf(literal, sizeof(literal), "  f32.const %g\n", (double)v);
            wat += literal;
            break;
        }
        case 2:
            wat += operand == 0 ? "  local.get $x\n"
                : operand == 1  ? "  local.get $y\n"
                : operand == 2  ? "  local.get $t\n"
                                : "  f32.const 0\n";
            break;
        case 3:
            wat += "  f32.add\n";
            break;
        case 4:
            wat += "  f32.sub\n";
            break;
        case 5:
            wat += "  f32.mul\n";
            break;
        case 6:
            wat += "  f32.div\n";
            break;
        case 7:
            wat += "  f32.min\n";
            break;
        case 8:
            wat += "  f32.max\n";
            break;
        case 9:
            wat += "  f32.abs\n";
            break;
        case 10:
            wat += "  f32.floor\n";
            break;
        case 11:
            wat += "  call $pow\n";
            break;
        case 12:
            wat += "  f32.sqrt\n";
            break;
        case 13:
            wat += "  f32.lt\n";
            break;
        case 14:
            wat += "  select\n";
            break;
        case 15:
            break;
        default:
            status = JFX_ERROR_NOT_IMPLEMENTED;
            break;
        }
        if (status != JFX_SUCCESS) {
            break;
        }
    }
    jolt_program_destroy(program);
    if (status != JFX_SUCCESS) {
        creative_set_error(f, "sketch: WASM export supports the basic scalar ops only");
        return status;
    }
    wat += " )\n (func $pow (param f32 f32) (result f32) f32.const 0)\n";
    wat += " (export \"pixel\" (func $pixel)))\n";
    jfx_result_t written =
        creative_write_file(path, reinterpret_cast<const uint8_t *>(wat.data()), wat.size());
    if (written != JFX_SUCCESS) {
        creative_set_error(f, "sketch: cannot write the WAT file");
    } else {
        f->creative_error[0] = 0;
    }
    return written;
}
extern "C" jfx_result_t jfx_desktop_frontend_creative_export(jfx_desktop_frontend_t *f,
    const char *kind, const char *path) {
    if (!f || !kind || !path || !path[0]) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!strcmp(kind, "obj") || !strcmp(kind, "o") || !strcmp(kind, "elf")) {
        return creative_export_object(f, path);
    }
    if (!strcmp(kind, "html") || !strcmp(kind, "canvas")) {
        return creative_export_html(f, path);
    }
    if (!strcmp(kind, "wasm") || !strcmp(kind, "wat")) {
        return creative_export_wat(f, path);
    }
    creative_set_error(f, "sketch: unknown export kind (obj, html, wasm)");
    return JFX_ERROR_INVALID_ARGUMENT;
}
/* Grades live in the selected clip's effect stack, so project persistence,
 * keyframes and rendering all use the same operators. */
static uint32_t grade_effect(jfx_desktop_frontend_t *f,const char *kind) {
    auto *t=jfx_editor_timeline(f->editor);
    for (uint32_t e=0;e<jfx_timeline_effect_count(t,f->selected_track,f->selected_clip);++e)
        if (std::strcmp(jfx_timeline_effect_kind(t,f->selected_track,f->selected_clip,e),kind)==0) return e;
    return jfx_timeline_add_effect(t,f->selected_track,f->selected_clip,kind);
}
extern "C" jfx_result_t jfx_desktop_frontend_color_grading_load_lut(jfx_desktop_frontend_t *f,const char *path) {
    if (!f || !path || std::strlen(path)>=sizeof(f->lut_path)) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_lut_t *lut=nullptr;
    auto r=jfx_lut_load_auto(path,&lut,f->status,sizeof(f->status));
    if (r!=JFX_SUCCESS) return r;
    uint32_t e=grade_effect(f,"lut");
    r=jfx_desktop_frontend_layer_effects_set_string(f,f->selected_track,f->selected_clip,e,0,path);
    if (r!=JFX_SUCCESS) { jfx_lut_destroy(lut); return r; }
    jfx_lut_destroy(f->grade_lut); f->grade_lut=lut;
    std::snprintf(f->lut_path,sizeof(f->lut_path),"%s",path);
    return jfx_desktop_frontend_layer_effects_set_param(f,f->selected_track,f->selected_clip,e,0,f->lut_mix);
}
extern "C" jfx_result_t jfx_desktop_frontend_color_grading_set_lut_mix(jfx_desktop_frontend_t *f,float v) {
    if (!f || !std::isfinite(v) || v<0 || v>1) return JFX_ERROR_INVALID_ARGUMENT;
    auto r=jfx_desktop_frontend_layer_effects_set_param(f,f->selected_track,f->selected_clip,grade_effect(f,"lut"),0,v);
    if (r==JFX_SUCCESS) f->lut_mix=v;
    return r;
}
extern "C" float jfx_desktop_frontend_color_grading_lut_mix(const jfx_desktop_frontend_t *f) { return f ? f->lut_mix : 0; }
extern "C" const jfx_lut_t *jfx_desktop_frontend_color_grading_lut(const jfx_desktop_frontend_t *f) { return f ? f->grade_lut : nullptr; }
extern "C" jfx_result_t jfx_desktop_frontend_color_grading_set_lift_gamma_gain(jfx_desktop_frontend_t *f,const float l[3],const float g[3],const float a[3]) {
    if (!f || !l || !g || !a) return JFX_ERROR_INVALID_ARGUMENT;
    for (int i=0;i<3;++i) if (!std::isfinite(l[i]) || !std::isfinite(g[i]) || !std::isfinite(a[i]) || l[i]<-1 || l[i]>1 || g[i]<0.1f || g[i]>4 || a[i]<0 || a[i]>4) return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t e=grade_effect(f,"lift_gamma_gain");
    if (e==UINT32_MAX) return JFX_ERROR_INVALID_ARGUMENT;
    for (size_t i=0;i<3;++i) {
        jfx_desktop_frontend_layer_effects_set_param(f,f->selected_track,f->selected_clip,e,i,l[i]);
        jfx_desktop_frontend_layer_effects_set_param(f,f->selected_track,f->selected_clip,e,i+3,g[i]);
        jfx_desktop_frontend_layer_effects_set_param(f,f->selected_track,f->selected_clip,e,i+6,a[i]);
        f->lift[i]=l[i]; f->gamma[i]=g[i]; f->gain[i]=a[i];
    }
    return edited(f,JFX_SUCCESS);
}
extern "C" jfx_result_t jfx_desktop_frontend_color_grading_get_lift_gamma_gain(const jfx_desktop_frontend_t *f,float l[3],float g[3],float a[3]) {
    if (!f || !l || !g || !a) return JFX_ERROR_INVALID_ARGUMENT;
    const auto *t=jfx_editor_timeline(f->editor);
    for (uint32_t e=0;e<jfx_timeline_effect_count(t,f->selected_track,f->selected_clip);++e)
        if (!std::strcmp(jfx_timeline_effect_kind(t,f->selected_track,f->selected_clip,e),"lift_gamma_gain")) {
            for (size_t c=0;c<3;++c) {
                l[c]=jfx_timeline_effect_param(t,f->selected_track,f->selected_clip,e,c);
                g[c]=jfx_timeline_effect_param(t,f->selected_track,f->selected_clip,e,c+3);
                a[c]=jfx_timeline_effect_param(t,f->selected_track,f->selected_clip,e,c+6);
            }
            return JFX_SUCCESS;
        }
    for (int c=0;c<3;++c) { l[c]=0; g[c]=a[c]=1; }
    return JFX_SUCCESS;
}
