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
    jolt_effects_destroy(frontend->effects);
    jfx_engine_shutdown(frontend->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
}

extern "C" jfx_result_t jfx_desktop_frontend_open_project(jfx_desktop_frontend_t *frontend,
    const char *path) {
    if (!frontend || !path || !path[0] || std::strlen(path) >= sizeof(frontend->project_path)) {
        return JFX_ERROR_INVALID_ARGUMENT;
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
    frontend->playing = false;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds) {
    if (!frontend || !std::isfinite(time_seconds) || time_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    frontend->time_seconds = time_seconds;
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
    static constexpr float kTimeline = 148.0f;
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
        ImGui::Begin("Properties");
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

/* Composes the menu bar and every panel. Called between NewFrame and Render. */
void compose_ui(jfx_desktop_frontend_t *frontend) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Project...")) {
                ImGui::OpenPopup("Open Project");
            }
            if (ImGui::MenuItem("Export Frame...", nullptr, false, !frontend->project_path[0])) {
                std::snprintf(frontend->status, sizeof(frontend->status),
                    "export writes the current preview as a PPM");
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
            ImGui::MenuItem("Properties", nullptr, &frontend->show_properties);
            ImGui::MenuItem("Console", nullptr, &frontend->show_console);
            ImGui::MenuItem("Statistics", nullptr, &frontend->show_stats);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    if (ImGui::BeginPopupModal("Open Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Path to a .jolt kernel:");
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
        ImGui::Text("Frame %llu  Playing: %s", (unsigned long long)frontend->frame_count,
            frontend->playing ? "yes" : "no");
        ImGui::End();
    }

    if (frontend->show_properties && open_panel(frontend, "Properties", PanelLayout::properties)) {
        if (ImGui::BeginCombo("Effect", frontend->effect_name)) {
            for (size_t i = 0; jolt_effects_name(i); ++i) {
                const bool selected = std::strcmp(jolt_effects_name(i), frontend->effect_name) == 0;
                if (ImGui::Selectable(jolt_effects_name(i), selected)) {
                    jfx_desktop_frontend_set_effect(frontend, jolt_effects_name(i),
                        frontend->effect_parameter);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        if (frontend->effect_has_parameter) {
            float parameter = frontend->effect_parameter;
            if (ImGui::SliderFloat("Amount", &parameter, frontend->effect_min, frontend->effect_max,
                    "%.3f")) {
                jfx_desktop_frontend_set_effect(frontend, frontend->effect_name, parameter);
            }
        } else {
            ImGui::TextDisabled("This effect takes no parameters.");
        }
        jfx_engine_caps_t caps;
        std::memset(&caps, 0, sizeof(caps));
        caps.size = sizeof(caps);
        jfx_engine_get_caps(frontend->engine, &caps);
        ImGui::Separator();
        ImGui::Text("Device: %s", caps.device_name);
        ImGui::Text("GPU available: %s", caps.gpu_available ? "yes" : "no");
        ImGui::Text("Last frame on GPU: %s", caps.used_gpu ? "yes" : "no");
        ImGui::End();
    }

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
                frontend->time_seconds = 0.0;
            }
            frontend->preview_dirty = true;
        }

        compose_ui(frontend);
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
