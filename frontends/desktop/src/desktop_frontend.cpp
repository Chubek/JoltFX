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

#include "host_window.h"
#include "imgui.h"
#include "joltscript/effects.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>

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
    bool editing, looping, show_grade, show_nodes;
    bool show_calibration;
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
    uint64_t audio_sample;
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

    /* Preview buffers. Allocated once at the preview resolution and reused
     * every frame so playback does not churn the allocator. */
    float *preview_input;
    float *preview_float;
    uint8_t *preview_rgba;
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
    frontend->show_viewport = true;
    frontend->show_timeline = true;
    frontend->show_properties = true;
    frontend->show_grade = true; frontend->show_nodes = true; frontend->looping = true;
    frontend->show_calibration = true;
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
    if (frontend) { jfx_export_destroy(frontend->export_job); reset_audio(frontend); }
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
    jfx_editor_destroy(frontend->editor);
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
    }
    std::strcpy(frontend->project_path, path);
    std::snprintf(frontend->project_input, sizeof(frontend->project_input), "%s", path);
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

extern "C" jfx_result_t jfx_desktop_frontend_play(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    frontend->playing = true;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_pause(jfx_desktop_frontend_t *frontend) {
    if (!frontend) return JFX_ERROR_INVALID_ARGUMENT;
    reset_audio(frontend);
    frontend->playing = false;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds) {
    if (!frontend || !std::isfinite(time_seconds) || time_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    reset_audio(frontend); frontend->time_seconds = time_seconds;
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
    if (width > kPreviewWidth || height > kPreviewHeight) return JFX_ERROR_INVALID_ARGUMENT;
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / (4u * sizeof(float))) return JFX_ERROR_OUT_OF_MEMORY;
    if (out_size < pixels * 4u) return JFX_ERROR_INVALID_ARGUMENT;

    if (frontend->editing) {
        if (frontend->node_preview_selected && jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_GRAPH)
            return jfx_editor_render_graph(frontend->editor,frontend->selected_node,frontend->time_seconds,width,height,out_rgba,out_size);
        return jfx_editor_render(frontend->editor, frontend->time_seconds, width, height, out_rgba, out_size);
    }

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

/* Panel geometry for the first frame. After that ImGui's own persistence takes
 * over, so a user-arranged layout survives restarts and this never fights the
 * user. */
struct PanelLayout {
    static constexpr float kMenuBar = 22.0f;
    static constexpr float kTimeline = 340.0f;
    static constexpr float kProperties = 320.0f;

    static void viewport() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0.0f, kMenuBar));
        ImGui::SetNextWindowSize(
            ImVec2(display.x - kProperties, display.y - kMenuBar - kTimeline));
        ImGui::Begin("Viewport");
    }
    static void properties() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(display.x - kProperties, kMenuBar));
        ImGui::SetNextWindowSize(ImVec2(kProperties, display.y - kMenuBar - kTimeline));
        ImGui::Begin("Layer Effects");
    }
    static void timeline() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0.0f, display.y - kTimeline));
        ImGui::SetNextWindowSize(ImVec2(display.x - kProperties, kTimeline));
        ImGui::Begin("Timeline");
    }
    static void console() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        const float area = display.y - kMenuBar - kTimeline;
        /* Lower half of the viewport column, so it does not cover the
         * Viewport's own title bar or status line. */
        ImGui::SetNextWindowPos(ImVec2(0.0f, kMenuBar + area * 0.5f));
        ImGui::SetNextWindowSize(ImVec2(420.0f, area * 0.5f));
        ImGui::Begin("Console");
    }
};

/* Opens `name`, applying the default geometry on the first frame only. */
template <typename LayoutFn>
bool open_panel(jfx_desktop_frontend_t *frontend, const char *name, LayoutFn layout) {
    if (frontend->layout_initialized) {
        return ImGui::Begin(name);
    }
    layout();
    return true;
}

#include "editor_panels.inc"

/* Composes the menu bar and every panel. Called between NewFrame and Render. */
void compose_ui(jfx_desktop_frontend_t *frontend) {
    bool open_requested=false;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...")) {
                open_requested=true;
            }
            if (ImGui::MenuItem("New sequence")) jfx_desktop_frontend_close_project(frontend);
            if (ImGui::MenuItem("New composition")) { jfx_desktop_frontend_node_compositing_new_graph(frontend); frontend->show_nodes=true; }
            if (ImGui::MenuItem("Save project...")) open_requested=true;
            if (ImGui::MenuItem("Export Frame...")) {
                auto *t=jfx_editor_timeline(frontend->editor);
                editor_result(frontend,jfx_editor_kind(frontend->editor)==JFX_PROJECT_KIND_GRAPH?
                    jfx_desktop_frontend_write_graph(frontend,UINT32_MAX,frontend->time_seconds,frontend->node_export_path):
                    jfx_editor_write_frame(frontend->editor,jfx_desktop_frontend_timeline_current_frame(frontend),
                        jfx_timeline_width(t),jfx_timeline_height(t),frontend->nle_export_path));
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit")) {
                frontend->quit_requested = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            if (ImGui::MenuItem("Play", "Space", frontend->playing)) {
                frontend->playing = !frontend->playing;
            }
            if (ImGui::MenuItem("Stop", "S")) {
                frontend->playing = false;
                frontend->time_seconds = 0.0;
                frontend->preview_dirty = true;
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

    if (frontend->show_viewport && open_panel(frontend, "Viewport", PanelLayout::viewport)) {
        ImGui::Text("Backend: %s", jfx_engine_backend_name(frontend->engine));
        ImGui::Text("Project: %s", frontend->project_path[0] ? frontend->project_path : "None");
        ImGui::Text("Surface: %ux%u", frontend->width, frontend->height);
        ImGui::Text("Status: %s", frontend->status);

        if (frontend->preview_dirty || frontend->playing) {
            if (jfx_desktop_frontend_render_rgba8(frontend, kPreviewWidth, kPreviewHeight,
                    frontend->preview_rgba,
                    (size_t)kPreviewWidth * kPreviewHeight * 4u) == JFX_SUCCESS) {
                frontend->preview_dirty = false;
            }
        }
        if (frontend->window) {
            void *texture = jfx_desktop_window_upload_rgba8(frontend->window, kPreviewWidth,
                kPreviewHeight, frontend->preview_rgba);
            if (texture) {
                frontend->preview_texture = texture;
                ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)),
                    ImVec2((float)kPreviewWidth, (float)kPreviewHeight));
            } else {
                ImGui::TextDisabled("Preview texture upload failed.");
            }
        } else {
            /* Headless: pixels are rendered through the backend, but there is no
             * window to present them in. Say so instead of faking a viewport. */
            ImGui::TextWrapped("Headless mode: %ux%u preview rendered through the %s backend, "
                               "but no window is attached to present it.",
                kPreviewWidth, kPreviewHeight, jfx_engine_backend_name(frontend->engine));
        }
        ImGui::End();
    }

    if (frontend->show_timeline && open_panel(frontend, "Timeline", PanelLayout::timeline)) {
        float time = (float)frontend->time_seconds;
        if (ImGui::SliderFloat("Time", &time, 0.0f, (float)frontend->duration_seconds, "%.3f s")) {
            frontend->time_seconds = time < 0.0f ? 0.0 : (double)time;
            frontend->preview_dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::Button(frontend->playing ? "Pause" : "Play", ImVec2(80, 0))) {
            frontend->playing = !frontend->playing;
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop", ImVec2(80, 0))) {
            frontend->playing = false;
            frontend->time_seconds = 0.0;
            frontend->preview_dirty = true;
        }
        timeline_tracks(frontend);
        ImGui::Text("Frame %llu  Playing: %s", (unsigned long long)frontend->frame_count,
            frontend->playing ? "yes" : "no");
        ImGui::End();
    }

    editor_panels(frontend);

    if (frontend->show_console && open_panel(frontend, "Console", PanelLayout::console)) {
        std::lock_guard<std::mutex> guard(g_console.mutex);
        if (ImGui::Button("Clear")) {
            g_console.next = 0;
            g_console.total = 0;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%u lines", g_console.total);
        ImGui::Separator();
        if (ImGui::BeginChild("log", ImVec2(0, -1), ImGuiChildFlags_None,
                ImGuiWindowFlags_HorizontalScrollbar)) {
            const uint32_t count = g_console.total < (uint32_t)kConsoleLines ? g_console.total
                                                                            : (uint32_t)kConsoleLines;
            const uint32_t first =
                (g_console.next + (uint32_t)kConsoleLines - count) % (uint32_t)kConsoleLines;
            for (uint32_t i = 0; i < count; ++i) {
                const uint32_t slot = (first + i) % (uint32_t)kConsoleLines;
                const tilly_log_level_t level = (tilly_log_level_t)g_console.level[slot];
                ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
                if (level == TILLY_LOG_ERROR || level == TILLY_LOG_FATAL) {
                    color = ImVec4(1.0f, 0.45f, 0.45f, 1.0f);
                } else if (level == TILLY_LOG_WARN) {
                    color = ImVec4(1.0f, 0.8f, 0.4f, 1.0f);
                } else if (level <= TILLY_LOG_DEBUG) {
                    color = ImVec4(0.6f, 0.65f, 0.75f, 1.0f);
                }
                ImGui::TextColored(color, "%s", g_console.lines[slot]);
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }

    if (frontend->show_stats) {
        jfx_engine_metrics_t metrics;
        std::memset(&metrics, 0, sizeof(metrics));
        metrics.size = sizeof(metrics);
        jfx_engine_get_metrics(frontend->engine, &metrics);
        ImGui::Begin("Statistics");
        ImGui::Text("Engine frames: %llu", (unsigned long long)metrics.frame_count);
        ImGui::Text("Last frame: %.3f ms", (double)metrics.last_frame_ns / 1.0e6);
        ImGui::Text("Worst frame: %.3f ms", (double)metrics.max_frame_ns / 1.0e6);
        ImGui::Text("UI frames: %llu", (unsigned long long)frontend->frame_count);
        ImGui::Text("Live buffers/textures/kernels: %u/%u/%u",
            jfx_engine_live_buffers(frontend->engine),
            jfx_engine_live_textures(frontend->engine),
            jfx_engine_live_kernels(frontend->engine));
        ImGui::End();
    }
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
        /* Advance the engine clock while playing, looping at the end of the
         * range so a short project keeps animating. */
        if (frontend->playing) {
            const ImGuiIO &io = ImGui::GetIO();
            const double delta = io.DeltaTime > 0.0f ? (double)io.DeltaTime : 1.0 / 60.0;
            frontend->time_seconds += delta;
            if (frontend->time_seconds > frontend->duration_seconds) {
                frontend->time_seconds = frontend->looping ? 0.0 : frontend->duration_seconds;
                if (!frontend->looping) frontend->playing = false;
            }
            frontend->preview_dirty = true;
        }

        if (frontend->editing && jfx_editor_kind(frontend->editor) == JFX_PROJECT_KIND_SEQUENCE)
            frontend->duration_seconds = (double)jfx_timeline_duration(jfx_editor_timeline(frontend->editor)) / jfx_timeline_fps(jfx_editor_timeline(frontend->editor));
        else if (frontend->editing) frontend->duration_seconds=JFX_DESKTOP_DEFAULT_DURATION;
        compose_ui(frontend);
        if (frontend->export_job && jfx_export_state(frontend->export_job)==JFX_EXPORT_RUNNING) {
            auto r=jfx_export_step(frontend->export_job,1);
            std::snprintf(frontend->status,sizeof(frontend->status),r==JFX_SUCCESS?"Video export: %llu / %llu frames":"Video export failed: %llu / %llu frames",
                (unsigned long long)jfx_export_completed_frames(frontend->export_job),(unsigned long long)jfx_export_total_frames(frontend->export_job));
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
                else if (jfx_desktop_window_queue_audio(frontend->window,pcm,4096)) frontend->audio_sample+=4096;
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
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    auto r=edited(f,jfx_editor_command(f->editor,op,a,b,c,v,text));
    if (r==JFX_SUCCESS && (!std::strcmp(op,"graph.new") || !std::strcmp(op,"sequence.new"))) {
        f->time_seconds=0; f->playing=false; f->selected_node=0; f->node_preview_selected=false; f->node_drag=0; f->node_wiring=false;
    }
    if (r==JFX_SUCCESS && (!std::strcmp(op,"undo") || !std::strcmp(op,"redo") || !std::strcmp(op,"node.remove"))) {
        f->node_preview_selected=false; f->node_drag=0; f->node_wiring=false;
        if (f->selected_node>=jfx_graph_node_count(jfx_editor_graph(f->editor))) f->selected_node=0;
    }
    return r;
}
extern "C" jfx_result_t jfx_desktop_frontend_sequence_state(jfx_desktop_frontend_t *f,char *out,size_t cap) {
    return f?jfx_editor_sequence_state(f->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_desktop_frontend_write_frame(jfx_desktop_frontend_t *f,uint64_t frame,const char *path) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    auto *t=jfx_editor_timeline(f->editor);
    return jfx_editor_write_frame(f->editor,frame,jfx_timeline_width(t),jfx_timeline_height(t),path);
}
extern "C" jfx_result_t jfx_desktop_frontend_export_begin(jfx_desktop_frontend_t *f,const jfx_export_options_t *o,jfx_export_job_t **out) {
    return jfx_export_begin(f?f->editor:nullptr,o,out);
}
extern "C" jfx_result_t jfx_desktop_frontend_audio_mixer(jfx_desktop_frontend_t *f,uint32_t rate,jfx_audio_mixer_t **out) {
    return jfx_audio_mixer_create(f?jfx_editor_timeline(f->editor):nullptr,rate,out);
}
extern "C" jfx_result_t jfx_desktop_frontend_set_panel_visible(jfx_desktop_frontend_t *f, jfx_desktop_panel_t p, bool v) {
    if (!f) return JFX_ERROR_INVALID_ARGUMENT;
    bool *panels[] = { &f->show_viewport, &f->show_timeline, &f->show_properties, &f->show_grade, &f->show_nodes, &f->show_console, &f->show_stats, &f->show_calibration };
    if ((unsigned)p >= JFX_DESKTOP_PANEL_COUNT) return JFX_ERROR_INVALID_ARGUMENT;
    *panels[p] = v; return JFX_SUCCESS;
}
extern "C" bool jfx_desktop_frontend_panel_visible(const jfx_desktop_frontend_t *f, jfx_desktop_panel_t p) {
    if (!f || (unsigned)p >= JFX_DESKTOP_PANEL_COUNT) return false;
    const bool panels[] = { f->show_viewport, f->show_timeline, f->show_properties, f->show_grade, f->show_nodes, f->show_console, f->show_stats, f->show_calibration };
    return panels[p];
}
extern "C" jfx_result_t jfx_desktop_frontend_save_project(jfx_desktop_frontend_t *f, const char *path) {
    if (!f || !path || !*path) return JFX_ERROR_INVALID_ARGUMENT;
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
    for (int i=0;i<3;++i) { l[i]=f->lift[i]; g[i]=f->gamma[i]; a[i]=f->gain[i]; } return JFX_SUCCESS;
}
