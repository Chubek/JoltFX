#ifndef JFX_DESKTOP_FRONTEND_H
#define JFX_DESKTOP_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"
#include "jfx/jfx_timeline.h"
#include "jfx/jfx_compose.h"
#include "jfx/jfx_lut.h"
#include "jfx/jfx_export.h"
#include "jfx/jfx_plugin_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_desktop_frontend jfx_desktop_frontend_t;

/* Panel identifiers for show/hide and layout persistence. */
typedef enum {
    JFX_DESKTOP_PANEL_VIEWPORT = 0,
    JFX_DESKTOP_PANEL_TIMELINE,
    JFX_DESKTOP_PANEL_LAYER_EFFECTS,   /* replaces the old Properties panel */
    JFX_DESKTOP_PANEL_COLOR_GRADING,
    JFX_DESKTOP_PANEL_NODE_COMPOSITING,
    JFX_DESKTOP_PANEL_CONSOLE,
    JFX_DESKTOP_PANEL_STATISTICS,
    JFX_DESKTOP_PANEL_COLOR_CALIBRATION,
    JFX_DESKTOP_PANEL_PLUGINS,
    JFX_DESKTOP_PANEL_AUDIO,
    JFX_DESKTOP_PANEL_COUNT
} jfx_desktop_panel_t;

/* Each editor interface occupies one workspace tab; transport and preview are shared. */
#define JFX_DESKTOP_API_MAJOR 1
#define JFX_DESKTOP_API_MINOR 2
#define JFX_DESKTOP_API_PATCH 0
typedef enum {
    JFX_DESKTOP_WORKSPACE_NLE = 0,
    JFX_DESKTOP_WORKSPACE_EFFECTS,
    JFX_DESKTOP_WORKSPACE_CALIBRATION,
    JFX_DESKTOP_WORKSPACE_GRADING,
    JFX_DESKTOP_WORKSPACE_COMPOSITING,
    JFX_DESKTOP_WORKSPACE_PLUGINS,
    JFX_DESKTOP_WORKSPACE_CONSOLE,
    JFX_DESKTOP_WORKSPACE_STATISTICS,
    JFX_DESKTOP_WORKSPACE_AUDIO,
    JFX_DESKTOP_WORKSPACE_COUNT
} jfx_desktop_workspace_t;

jfx_result_t jfx_desktop_frontend_set_workspace(jfx_desktop_frontend_t *frontend,
    jfx_desktop_workspace_t workspace);
jfx_desktop_workspace_t jfx_desktop_frontend_workspace(const jfx_desktop_frontend_t *frontend);
/* Borrowed host; destroyed after this frontend's documents/jobs. */
jfx_plugin_host_t *jfx_desktop_frontend_plugins(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_load_plugin(jfx_desktop_frontend_t *frontend,const char *path,uint32_t *out_plugin_id);
jfx_result_t jfx_desktop_frontend_unload_plugin(jfx_desktop_frontend_t *frontend,uint32_t plugin_id);
jfx_result_t jfx_desktop_frontend_invoke_plugin(jfx_desktop_frontend_t *frontend,const char *action);

typedef struct {
    size_t size;                   /* set to sizeof(jfx_desktop_frontend_config_t) */
    uint32_t width;                /* 0 selects 1280 */
    uint32_t height;               /* 0 selects 720 */
    const char *backend_name;      /* "vulkan"/"metal"/"d3d12"/"webgpu"/"auto"/NULL */
    const char *project_path;      /* optional .jolt kernel or .jfx project to load at startup */
    const char *effect_name;       /* optional bundled effect to preview */
    float effect_parameter;        /* effect parameter, clamped by the catalog */
    bool show_timeline;            /* initial panel visibility */
    bool show_layer_effects;
    bool show_color_grading;
    bool show_node_compositing;
    bool show_console;
    bool show_statistics;
} jfx_desktop_frontend_config_t;

#define JFX_DESKTOP_DEFAULT_WIDTH 1280u
#define JFX_DESKTOP_DEFAULT_HEIGHT 720u
#define JFX_DESKTOP_DEFAULT_DURATION 10.0

jfx_result_t jfx_desktop_frontend_create(const jfx_desktop_frontend_config_t *config,
    jfx_desktop_frontend_t **out_frontend);
void jfx_desktop_frontend_destroy(jfx_desktop_frontend_t *frontend);

jfx_result_t jfx_desktop_frontend_open_project(jfx_desktop_frontend_t *frontend,
    const char *path);
jfx_result_t jfx_desktop_frontend_save_project(jfx_desktop_frontend_t *frontend,
    const char *path);
jfx_result_t jfx_desktop_frontend_close_project(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_resize(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height);
jfx_result_t jfx_desktop_frontend_play(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_pause(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds);

/* Panel management */
jfx_result_t jfx_desktop_frontend_set_panel_visible(jfx_desktop_frontend_t *frontend,
    jfx_desktop_panel_t panel, bool visible);
bool jfx_desktop_frontend_panel_visible(const jfx_desktop_frontend_t *frontend,
    jfx_desktop_panel_t panel);

/* Timeline control (delegates to the internal timeline) */
jfx_result_t jfx_desktop_frontend_edit(jfx_desktop_frontend_t *frontend,const char *op,
    uint32_t a,uint32_t b,uint32_t c,double value,const char *text);
jfx_result_t jfx_desktop_frontend_sequence_state(jfx_desktop_frontend_t *frontend,char *out_json,size_t capacity);
jfx_result_t jfx_desktop_frontend_write_frame(jfx_desktop_frontend_t *frontend,uint64_t frame,const char *path);
jfx_result_t jfx_desktop_frontend_export_begin(jfx_desktop_frontend_t *frontend,const jfx_export_options_t *options,jfx_export_job_t **out_job);
jfx_result_t jfx_desktop_frontend_audio_mixer(jfx_desktop_frontend_t *frontend,uint32_t rate,jfx_audio_mixer_t **out_mixer);
jfx_result_t jfx_desktop_frontend_timeline_play(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_timeline_pause(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_timeline_seek(jfx_desktop_frontend_t *frontend, double time_seconds);
jfx_result_t jfx_desktop_frontend_timeline_set_loop(jfx_desktop_frontend_t *frontend, bool loop);
double jfx_desktop_frontend_timeline_duration(const jfx_desktop_frontend_t *frontend);
uint64_t jfx_desktop_frontend_timeline_current_frame(const jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_timeline_set_current_frame(jfx_desktop_frontend_t *frontend, uint64_t frame);

/* Layer-based Effects panel (per-clip effect stack) */
jfx_result_t jfx_desktop_frontend_layer_effects_add(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, const char *kind_name);
jfx_result_t jfx_desktop_frontend_layer_effects_remove(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect);
jfx_result_t jfx_desktop_frontend_layer_effects_move(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, uint32_t to_index);
jfx_result_t jfx_desktop_frontend_layer_effects_set_enabled(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, bool enabled);
jfx_result_t jfx_desktop_frontend_layer_effects_set_opacity(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, float opacity);
jfx_result_t jfx_desktop_frontend_layer_effects_set_blend(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, jfx_blend_mode_t mode);
jfx_result_t jfx_desktop_frontend_layer_effects_set_param(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, size_t param, float value);
jfx_result_t jfx_desktop_frontend_layer_effects_set_string(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, size_t index, const char *text);
jfx_result_t jfx_desktop_frontend_layer_effects_add_key(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, size_t param, uint64_t frame, float value);
jfx_result_t jfx_desktop_frontend_layer_effects_remove_key(jfx_desktop_frontend_t *frontend,
    uint32_t track, uint32_t clip, uint32_t effect, size_t param, uint64_t frame);

/* Color Grading panel (LUT-based) */
jfx_result_t jfx_desktop_frontend_color_grading_load_lut(jfx_desktop_frontend_t *frontend,
    const char *path);
jfx_result_t jfx_desktop_frontend_color_grading_set_lut_mix(jfx_desktop_frontend_t *frontend,
    float mix);
float jfx_desktop_frontend_color_grading_lut_mix(const jfx_desktop_frontend_t *frontend);
const jfx_lut_t *jfx_desktop_frontend_color_grading_lut(const jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_color_grading_set_lift_gamma_gain(jfx_desktop_frontend_t *frontend,
    const float lift[3], const float gamma[3], const float gain[3]);
jfx_result_t jfx_desktop_frontend_color_grading_get_lift_gamma_gain(const jfx_desktop_frontend_t *frontend,
    float lift[3], float gamma[3], float gain[3]);

/* Node Compositing panel */
jfx_result_t jfx_desktop_frontend_node_compositing_new_graph(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_node_compositing_add_node(jfx_desktop_frontend_t *frontend,
    const char *kind_name, const char *label, uint32_t *out_node);
jfx_result_t jfx_desktop_frontend_node_compositing_remove_node(jfx_desktop_frontend_t *frontend,
    uint32_t node);
jfx_result_t jfx_desktop_frontend_node_compositing_connect(jfx_desktop_frontend_t *frontend,
    uint32_t from_node, size_t from_port, uint32_t to_node, size_t to_port);
jfx_result_t jfx_desktop_frontend_node_compositing_disconnect(jfx_desktop_frontend_t *frontend,
    uint32_t to_node, size_t to_port);
jfx_result_t jfx_desktop_frontend_node_compositing_set_param(jfx_desktop_frontend_t *frontend,
    uint32_t node, size_t param, float value);
jfx_result_t jfx_desktop_frontend_node_compositing_set_string(jfx_desktop_frontend_t *frontend,
    uint32_t node, size_t index, const char *text);
jfx_result_t jfx_desktop_frontend_node_compositing_set_output(jfx_desktop_frontend_t *frontend,
    uint32_t node);
uint32_t jfx_desktop_frontend_node_compositing_output(const jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_node_compositing_render(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height, uint8_t *out_rgba, size_t out_size);
jfx_result_t jfx_desktop_frontend_graph_state(jfx_desktop_frontend_t *frontend,char *out_json,size_t capacity);
jfx_result_t jfx_desktop_frontend_render_graph(jfx_desktop_frontend_t *frontend,uint32_t node,double seconds,
    uint32_t width,uint32_t height,uint8_t *out_rgba,size_t capacity);
jfx_result_t jfx_desktop_frontend_write_graph(jfx_desktop_frontend_t *frontend,uint32_t node,double seconds,const char *path);

/* Legacy effect preview (kept for compatibility) */
jfx_result_t jfx_desktop_frontend_set_effect(jfx_desktop_frontend_t *frontend,
    const char *effect_name, float parameter);

/* Composes one UI frame and advances the engine by one tick. Works with or
 * without a host window, so it is the headless smoke path as well. */
jfx_result_t jfx_desktop_frontend_draw(jfx_desktop_frontend_t *frontend);

/* True once the user has asked to quit from the File menu or closed the
 * window; the run loop stops when it becomes true. */
bool jfx_desktop_frontend_should_quit(const jfx_desktop_frontend_t *frontend);

/* Runs the interactive loop until the window is closed, the user quits, or one
 * of the limits is reached. `max_frames` of 0 means "until closed" and
 * `max_seconds` of 0 means "no time limit". Requires a host window; returns
 * JFX_ERROR_NOT_INITIALIZED when none is available. */
jfx_result_t jfx_desktop_frontend_run(jfx_desktop_frontend_t *frontend, uint64_t max_frames,
    double max_seconds);

const char *jfx_desktop_frontend_project_path(const jfx_desktop_frontend_t *frontend);
/* Backend the engine actually resolved ("auto" resolves to a real name). */
const char *jfx_desktop_frontend_backend_name(const jfx_desktop_frontend_t *frontend);
const char *jfx_desktop_frontend_effect_name(const jfx_desktop_frontend_t *frontend);
float jfx_desktop_frontend_effect_parameter(const jfx_desktop_frontend_t *frontend);
double jfx_desktop_frontend_time(const jfx_desktop_frontend_t *frontend);
bool jfx_desktop_frontend_playing(const jfx_desktop_frontend_t *frontend);
uint32_t jfx_desktop_frontend_width(const jfx_desktop_frontend_t *frontend);
uint32_t jfx_desktop_frontend_height(const jfx_desktop_frontend_t *frontend);
uint64_t jfx_desktop_frontend_frame_count(const jfx_desktop_frontend_t *frontend);

/* Renders the current preview through the engine's backend into tightly packed
 * RGBA8. `width`/`height` set the preview resolution; the next draw resizes the
 * GL texture to match. Output is untouched on error. */
jfx_result_t jfx_desktop_frontend_render_rgba8(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height, uint8_t *out_rgba, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* JFX_DESKTOP_FRONTEND_H */
