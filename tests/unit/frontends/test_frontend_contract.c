/* Shared frontend contract.
 *
 * The dispatchers must turn a missing operation into a uniform
 * JFX_ERROR_NOT_IMPLEMENTED, a bad argument into
 * JFX_ERROR_INVALID_ARGUMENT, and must report capabilities that match the ops
 * table exactly. A frontend that supplies a NULL entry point is the normal
 * case, not an error, so it must be safe everywhere. */

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "jfx_frontend.h"

/* ---- A minimal frontend: playback and state, nothing else. ---------------- */

typedef struct {
    double time_seconds;
    bool playing;
    bool looping;
    int shutdown_calls;
} minimal_state_t;

static jfx_result_t minimal_init(const jfx_frontend_desc_t *desc, void **out_state) {
    if (desc->size < sizeof(*desc)) return JFX_ERROR_VERSION_MISMATCH;
    *out_state = NULL;
    minimal_state_t *state = (minimal_state_t *)calloc(1, sizeof(*state));
    if (!state) return JFX_ERROR_OUT_OF_MEMORY;
    *out_state = state;
    return JFX_SUCCESS;
}

static void minimal_shutdown(void *state) {
    if (!state) return;
    minimal_state_t *minimal = (minimal_state_t *)state;
    ++minimal->shutdown_calls;
    free(minimal);
}

static jfx_result_t minimal_play(void *state) {
    minimal_state_t *minimal = (minimal_state_t *)state;
    if (!minimal) return JFX_ERROR_INVALID_ARGUMENT;
    minimal->playing = true;
    return JFX_SUCCESS;
}

static jfx_result_t minimal_pause(void *state) {
    minimal_state_t *minimal = (minimal_state_t *)state;
    if (!minimal) return JFX_ERROR_INVALID_ARGUMENT;
    minimal->playing = false;
    return JFX_SUCCESS;
}

static jfx_result_t minimal_seek(void *state, double time_seconds) {
    minimal_state_t *minimal = (minimal_state_t *)state;
    if (!minimal || !isfinite(time_seconds) || time_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    minimal->time_seconds = time_seconds;
    return JFX_SUCCESS;
}

static jfx_result_t minimal_get_viewport_state(void *state, jfx_viewport_state_t *out_state) {
    minimal_state_t *minimal = (minimal_state_t *)state;
    if (!minimal || !out_state) return JFX_ERROR_INVALID_ARGUMENT;
    memset(out_state, 0, sizeof(*out_state));
    out_state->time_seconds = minimal->time_seconds;
    out_state->playing = minimal->playing;
    out_state->looping = minimal->looping;
    return JFX_SUCCESS;
}

static const jfx_frontend_ops_t kMinimalOps = {
    .init = minimal_init,
    .shutdown = minimal_shutdown,
    .play = minimal_play,
    .pause = minimal_pause,
    .seek = minimal_seek,
    .get_viewport_state = minimal_get_viewport_state,
};

/* ---- A frontend that fails to produce a state handle. --------------------- */

static jfx_result_t lying_init(const jfx_frontend_desc_t *desc, void **out_state) {
    (void)desc;
    *out_state = NULL;
    return JFX_SUCCESS;
}

static void noop_shutdown(void *state) { (void)state; }

static const jfx_frontend_ops_t kLyingOps = {
    .init = lying_init,
    .shutdown = noop_shutdown,
};

int main(void) {
    jfx_frontend_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.name = "minimal";
    desc.width = 64;
    desc.height = 64;

    /* Argument validation. */
    jfx_frontend_t *frontend = NULL;
    assert(jfx_frontend_init(NULL, "x", &desc, &frontend) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_init(&kMinimalOps, NULL, &desc, &frontend) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_init(&kMinimalOps, "x", NULL, &frontend) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_init(&kMinimalOps, "x", &desc, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(frontend == NULL);
    jfx_frontend_desc_t stale = desc;
    stale.size = 0;
    assert(jfx_frontend_init(&kMinimalOps, "x", &stale, &frontend) ==
        JFX_ERROR_VERSION_MISMATCH);
    static const jfx_frontend_ops_t kNoLifecycle = { 0 };
    assert(jfx_frontend_init(&kNoLifecycle, "x", &desc, &frontend) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* An init that reports success without a handle must not yield a frontend
     * that crashes on first use. */
    assert(jfx_frontend_init(&kLyingOps, "lying", &desc, &frontend) ==
        JFX_ERROR_NOT_INITIALIZED);
    assert(frontend == NULL);

    assert(jfx_frontend_init(&kMinimalOps, "minimal", &desc, &frontend) == JFX_SUCCESS);
    assert(frontend != NULL);
    assert(strcmp(frontend->name, "minimal") == 0);
    assert(frontend->ops == &kMinimalOps);
    assert(frontend->state != NULL);

    /* Capabilities mirror the ops table exactly. */
    const uint32_t capabilities = jfx_frontend_capabilities(frontend);
    assert(capabilities & JFX_FRONTEND_CAP_PLAYBACK);
    assert(capabilities & JFX_FRONTEND_CAP_VIEWPORT_STATE);
    assert(!(capabilities & JFX_FRONTEND_CAP_RUN));
    assert(!(capabilities & JFX_FRONTEND_CAP_OPEN_PROJECT));
    assert(!(capabilities & JFX_FRONTEND_CAP_SAVE_PROJECT));
    assert(!(capabilities & JFX_FRONTEND_CAP_CLOSE_PROJECT));
    assert(!(capabilities & JFX_FRONTEND_CAP_LOOP));
    assert(!(capabilities & JFX_FRONTEND_CAP_RENDER));
    assert(!(capabilities & JFX_FRONTEND_CAP_EXPORT));
    assert(!(capabilities & JFX_FRONTEND_CAP_SELECTION));
    assert(jfx_frontend_has_capabilities(frontend,
        JFX_FRONTEND_CAP_PLAYBACK | JFX_FRONTEND_CAP_VIEWPORT_STATE));
    assert(!jfx_frontend_has_capabilities(frontend, JFX_FRONTEND_CAP_EXPORT));
    assert(jfx_frontend_capabilities(NULL) == 0u);

    assert(strcmp(jfx_frontend_capability_name(JFX_FRONTEND_CAP_RENDER), "render") == 0);
    assert(strcmp(jfx_frontend_capability_name(JFX_FRONTEND_CAP_EXPORT), "export") == 0);
    assert(strcmp(jfx_frontend_capability_name(1u << 20), "unknown") == 0);

    /* Implemented operations dispatch. */
    assert(jfx_frontend_play(frontend) == JFX_SUCCESS);
    assert(jfx_frontend_pause(frontend) == JFX_SUCCESS);
    assert(jfx_frontend_seek(frontend, 2.5) == JFX_SUCCESS);
    assert(jfx_frontend_seek(frontend, -1.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_seek(frontend, NAN) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_viewport_state_t state;
    assert(jfx_frontend_get_viewport_state(frontend, &state) == JFX_SUCCESS);
    assert(state.time_seconds == 2.5);
    assert(state.playing == false);

    /* Missing operations report NOT_IMPLEMENTED, never crash. */
    jfx_frontend_run(frontend); /* a frontend with no loop is a no-op, not an error */
    assert(jfx_frontend_open_project(frontend, "a.jolt") == JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_save_project(frontend, "a.jolt") == JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_close_project(frontend) == JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_set_loop(frontend, true) == JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_set_selection(frontend, "node") == JFX_ERROR_NOT_IMPLEMENTED);

    uint8_t frame[64 * 64 * 4];
    assert(jfx_frontend_render_frame(frontend, 64, 64, frame, sizeof(frame)) ==
        JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_resize_viewport(frontend, 64, 64) == JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_set_zoom(frontend, 1.0) == JFX_ERROR_NOT_IMPLEMENTED);
    assert(jfx_frontend_export_frames(frontend, "out", 64, 64, 0, 1, NULL, NULL) ==
        JFX_ERROR_NOT_IMPLEMENTED);
    char selection[16];
    assert(jfx_frontend_get_selection(frontend, selection, sizeof(selection)) ==
        JFX_ERROR_NOT_IMPLEMENTED);

    /* Argument validation happens before the capability check, so a bad call
     * is reported as a bad call. */
    assert(jfx_frontend_open_project(frontend, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_open_project(frontend, "") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_render_frame(frontend, 0, 64, frame, sizeof(frame)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_render_frame(frontend, 64, 64, NULL, sizeof(frame)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_render_frame(frontend, 64, 64, frame, 4) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_resize_viewport(frontend, 0, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_set_zoom(frontend, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_set_zoom(frontend, NAN) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_export_frames(frontend, "", 64, 64, 0, 1, NULL, NULL) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_export_frames(frontend, "out", 64, 64, 5, 1, NULL, NULL) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_get_selection(frontend, NULL, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_get_viewport_state(frontend, NULL) == JFX_ERROR_INVALID_ARGUMENT);

    /* Every dispatcher rejects a NULL frontend. */
    assert(jfx_frontend_play(NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_pause(NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_seek(NULL, 0.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_set_loop(NULL, true) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_open_project(NULL, "a") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_save_project(NULL, "a") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_close_project(NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_render_frame(NULL, 1, 1, frame, sizeof(frame)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_resize_viewport(NULL, 1, 1) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_set_zoom(NULL, 1.0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_export_frames(NULL, "o", 1, 1, 0, 0, NULL, NULL) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_get_selection(NULL, selection, sizeof(selection)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_set_selection(NULL, "n") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_frontend_get_viewport_state(NULL, &state) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_frontend_run(NULL);
    jfx_frontend_shutdown(NULL);

    jfx_frontend_shutdown(frontend);
    return 0;
}
