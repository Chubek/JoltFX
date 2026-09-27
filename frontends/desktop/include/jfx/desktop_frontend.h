#ifndef JFX_DESKTOP_FRONTEND_H
#define JFX_DESKTOP_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct jfx_desktop_frontend jfx_desktop_frontend_t;

typedef struct {
    size_t size;                   /* set to sizeof(jfx_desktop_frontend_config_t) */
    uint32_t width;                /* 0 selects 1280 */
    uint32_t height;               /* 0 selects 720 */
    const char *backend_name;      /* "vulkan"/"metal"/"d3d12"/"webgpu"/"auto"/NULL */
    const char *project_path;      /* optional .jolt kernel to load at startup */
    const char *effect_name;       /* optional bundled effect to preview */
    float effect_parameter;        /* effect parameter, clamped by the catalog */
} jfx_desktop_frontend_config_t;

#define JFX_DESKTOP_DEFAULT_WIDTH 1280u
#define JFX_DESKTOP_DEFAULT_HEIGHT 720u
#define JFX_DESKTOP_DEFAULT_DURATION 10.0

jfx_result_t jfx_desktop_frontend_create(const jfx_desktop_frontend_config_t *config,
    jfx_desktop_frontend_t **out_frontend);
void jfx_desktop_frontend_destroy(jfx_desktop_frontend_t *frontend);

jfx_result_t jfx_desktop_frontend_open_project(jfx_desktop_frontend_t *frontend,
    const char *path);
jfx_result_t jfx_desktop_frontend_resize(jfx_desktop_frontend_t *frontend,
    uint32_t width, uint32_t height);
jfx_result_t jfx_desktop_frontend_play(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_pause(jfx_desktop_frontend_t *frontend);
jfx_result_t jfx_desktop_frontend_seek(jfx_desktop_frontend_t *frontend,
    double time_seconds);

/* Selects the previewed bundled effect. parameter is clamped to the effect's
 * documented range. Rejects unknown effect names. */
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

#endif
