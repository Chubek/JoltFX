#ifndef JFX_FRONTEND_H
#define JFX_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The contract every JoltFX frontend implements.
 *
 * A frontend is a table of operations over an opaque, implementation-private
 * state handle. Every entry point validates its arguments and returns
 * jfx_result_t; none of them abort on bad input. An operation a frontend does
 * not provide is left NULL and reported through jfx_frontend_capabilities,
 * rather than filled in with a stub that appears to work.
 *
 *   CLI        open/save/close, playback, render_frame, export. No loop, no UI.
 *   Desktop    the full set.
 *   Web        read-only: playback and rendering, no save or export.
 *   Mobile     playback, gestures, viewport. No selection.
 *
 * Callers use the jfx_frontend_* dispatchers below rather than the table
 * directly: they turn a missing operation into one uniform
 * JFX_ERROR_NOT_IMPLEMENTED instead of a NULL dereference. */

typedef struct jfx_frontend jfx_frontend_t;

/* ---- Description --------------------------------------------------------- */

typedef struct jfx_frontend_desc {
    size_t size;                 /* set to sizeof(jfx_frontend_desc_t) */
    const char *name;            /* "cli", "desktop", "web", "mobile" */
    uint32_t width;              /* 0 selects the frontend's default */
    uint32_t height;
    const char *backend_name;    /* NULL selects "auto" */
} jfx_frontend_desc_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    double time_seconds;
    double duration_seconds;
    bool playing;
    bool looping;
    double zoom;             /* viewport zoom, 1.0 is native size */
} jfx_viewport_state_t;

/* Progress callback: return 0 to continue, non-zero to cancel the export. */
typedef int (*jfx_export_progress_fn)(size_t current_frame, size_t total_frames,
    double elapsed_seconds, void *user_data);

/* ---- Operations ----------------------------------------------------------
 *
 * Each takes the private state handle produced by init(). Leaving a pointer
 * NULL is how a frontend declines an operation.
 */
typedef struct jfx_frontend_ops {
    /* Lifecycle. init must publish *out_state or return an error; the
     * dispatcher frees the wrapper if it does not. */
    jfx_result_t (*init)(const jfx_frontend_desc_t *desc, void **out_state);
    void (*shutdown)(void *state);
    /* Runs the frontend's own loop. Frontends with no loop (the CLI) leave
     * this NULL. */
    void (*run)(void *state);

    /* Project management */
    jfx_result_t (*open_project)(void *state, const char *path);
    jfx_result_t (*save_project)(void *state, const char *path);
    jfx_result_t (*close_project)(void *state);

    /* Playback */
    jfx_result_t (*play)(void *state);
    jfx_result_t (*pause)(void *state);
    jfx_result_t (*seek)(void *state, double time_seconds);
    jfx_result_t (*set_loop)(void *state, bool loop);

    /* Viewport */
    /* Renders the current frame into tightly packed RGBA8 of
     * width * height * 4 bytes. Output is untouched on error. */
    jfx_result_t (*render_frame)(void *state, uint32_t width, uint32_t height,
        uint8_t *out_rgba, size_t out_size);
    jfx_result_t (*resize_viewport)(void *state, uint32_t width, uint32_t height);
    jfx_result_t (*set_zoom)(void *state, double zoom);

    /* Export: writes one binary PPM per frame into output_directory. */
    jfx_result_t (*export_frames)(void *state, const char *output_directory,
        uint32_t width, uint32_t height, uint32_t start_frame, uint32_t end_frame,
        jfx_export_progress_fn progress_cb, void *user_data);

    /* UI state */
    jfx_result_t (*get_selection)(void *state, char *out_buffer, size_t buffer_size);
    jfx_result_t (*set_selection)(void *state, const char *selection);
    jfx_result_t (*get_viewport_state)(void *state, jfx_viewport_state_t *out_state);
} jfx_frontend_ops_t;

struct jfx_frontend {
    const char *name;                /* implementation identity, e.g. "desktop" */
    const jfx_frontend_ops_t *ops;   /* never NULL after a successful init */
    void *state;                     /* implementation-private */
};

/* ---- Capability reporting ------------------------------------------------ */

typedef enum {
    JFX_FRONTEND_CAP_RUN = 1u << 0,
    JFX_FRONTEND_CAP_OPEN_PROJECT = 1u << 1,
    JFX_FRONTEND_CAP_SAVE_PROJECT = 1u << 2,
    JFX_FRONTEND_CAP_CLOSE_PROJECT = 1u << 3,
    JFX_FRONTEND_CAP_PLAYBACK = 1u << 4,
    JFX_FRONTEND_CAP_LOOP = 1u << 5,
    JFX_FRONTEND_CAP_RENDER = 1u << 6,
    JFX_FRONTEND_CAP_EXPORT = 1u << 7,
    JFX_FRONTEND_CAP_SELECTION = 1u << 8,
    JFX_FRONTEND_CAP_VIEWPORT_STATE = 1u << 9,
    JFX_FRONTEND_CAP_ZOOM = 1u << 10
} jfx_frontend_capability_t;

/* The capability bitmask implied by `frontend`'s ops table. */
uint32_t jfx_frontend_capabilities(const jfx_frontend_t *frontend);

/* Human-readable name of a single capability bit, or "unknown". */
const char *jfx_frontend_capability_name(uint32_t capability);

/* True when every bit in `required` is present. */
bool jfx_frontend_has_capabilities(const jfx_frontend_t *frontend, uint32_t required);

/* ---- Safe dispatch ------------------------------------------------------- */

jfx_result_t jfx_frontend_init(const jfx_frontend_ops_t *ops, const char *name,
    const jfx_frontend_desc_t *desc, jfx_frontend_t **out_frontend);
void jfx_frontend_shutdown(jfx_frontend_t *frontend);
void jfx_frontend_run(jfx_frontend_t *frontend);
jfx_result_t jfx_frontend_open_project(jfx_frontend_t *frontend, const char *path);
jfx_result_t jfx_frontend_save_project(jfx_frontend_t *frontend, const char *path);
jfx_result_t jfx_frontend_close_project(jfx_frontend_t *frontend);
jfx_result_t jfx_frontend_play(jfx_frontend_t *frontend);
jfx_result_t jfx_frontend_pause(jfx_frontend_t *frontend);
jfx_result_t jfx_frontend_seek(jfx_frontend_t *frontend, double time_seconds);
jfx_result_t jfx_frontend_set_loop(jfx_frontend_t *frontend, bool loop);
jfx_result_t jfx_frontend_render_frame(jfx_frontend_t *frontend, uint32_t width,
    uint32_t height, uint8_t *out_rgba, size_t out_size);
jfx_result_t jfx_frontend_resize_viewport(jfx_frontend_t *frontend, uint32_t width,
    uint32_t height);
jfx_result_t jfx_frontend_set_zoom(jfx_frontend_t *frontend, double zoom);
jfx_result_t jfx_frontend_export_frames(jfx_frontend_t *frontend,
    const char *output_directory, uint32_t width, uint32_t height, uint32_t start_frame,
    uint32_t end_frame, jfx_export_progress_fn progress_cb, void *user_data);
jfx_result_t jfx_frontend_get_selection(jfx_frontend_t *frontend, char *out_buffer,
    size_t buffer_size);
jfx_result_t jfx_frontend_set_selection(jfx_frontend_t *frontend, const char *selection);
jfx_result_t jfx_frontend_get_viewport_state(jfx_frontend_t *frontend,
    jfx_viewport_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif /* JFX_FRONTEND_H */
