/* Shared frontend contract.
 *
 * Holds the capability bitmask and the safe dispatchers every frontend routes
 * through. The dispatchers exist so "this frontend does not do that" is one
 * uniform JFX_ERROR_NOT_IMPLEMENTED rather than a NULL dereference at a call
 * site that assumed the operation existed. */

#include "jfx_frontend.h"

#include <stdio.h>
#include <string.h>

#include "tilly/allocator.h"

static void *frontend_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

/* Derived from the ops table so the two can never disagree. */
uint32_t jfx_frontend_capabilities(const jfx_frontend_t *frontend) {
    if (!frontend || !frontend->ops) {
        return 0u;
    }
    const jfx_frontend_ops_t *ops = frontend->ops;
    uint32_t capabilities = 0u;
    if (ops->run) capabilities |= JFX_FRONTEND_CAP_RUN;
    if (ops->open_project) capabilities |= JFX_FRONTEND_CAP_OPEN_PROJECT;
    if (ops->save_project) capabilities |= JFX_FRONTEND_CAP_SAVE_PROJECT;
    if (ops->close_project) capabilities |= JFX_FRONTEND_CAP_CLOSE_PROJECT;
    if (ops->play && ops->pause && ops->seek) capabilities |= JFX_FRONTEND_CAP_PLAYBACK;
    if (ops->set_loop) capabilities |= JFX_FRONTEND_CAP_LOOP;
    if (ops->render_frame) capabilities |= JFX_FRONTEND_CAP_RENDER;
    if (ops->export_frames) capabilities |= JFX_FRONTEND_CAP_EXPORT;
    if (ops->get_selection && ops->set_selection) capabilities |= JFX_FRONTEND_CAP_SELECTION;
    if (ops->get_viewport_state) capabilities |= JFX_FRONTEND_CAP_VIEWPORT_STATE;
    return capabilities;
}

const char *jfx_frontend_capability_name(uint32_t capability) {
    switch (capability) {
    case JFX_FRONTEND_CAP_RUN: return "run";
    case JFX_FRONTEND_CAP_OPEN_PROJECT: return "open_project";
    case JFX_FRONTEND_CAP_SAVE_PROJECT: return "save_project";
    case JFX_FRONTEND_CAP_CLOSE_PROJECT: return "close_project";
    case JFX_FRONTEND_CAP_PLAYBACK: return "playback";
    case JFX_FRONTEND_CAP_LOOP: return "loop";
    case JFX_FRONTEND_CAP_RENDER: return "render";
    case JFX_FRONTEND_CAP_EXPORT: return "export";
    case JFX_FRONTEND_CAP_SELECTION: return "selection";
    case JFX_FRONTEND_CAP_VIEWPORT_STATE: return "viewport_state";
    default: return "unknown";
    }
}

bool jfx_frontend_has_capabilities(const jfx_frontend_t *frontend, uint32_t required) {
    return (jfx_frontend_capabilities(frontend) & required) == required;
}

jfx_result_t jfx_frontend_init(const jfx_frontend_ops_t *ops, const char *name,
    const jfx_frontend_desc_t *desc, jfx_frontend_t **out_frontend) {
    if (!ops || !name || !desc || !out_frontend) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_frontend = NULL;
    if (desc->size < sizeof(*desc)) {
        return JFX_ERROR_VERSION_MISMATCH;
    }
    if (!ops->init || !ops->shutdown) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    jfx_frontend_t *frontend = frontend_alloc(sizeof(*frontend));
    if (!frontend) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(frontend, 0, sizeof(*frontend));
    frontend->name = name;
    frontend->ops = ops;

    void *state = NULL;
    jfx_result_t status = ops->init(desc, &state);
    if (status != JFX_SUCCESS) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return status;
    }
    if (!state) {
        /* The implementation reported success without producing a handle; treat
         * it as a failure rather than handing back a frontend that crashes on
         * first use. */
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
        return JFX_ERROR_NOT_INITIALIZED;
    }
    frontend->state = state;
    *out_frontend = frontend;
    return JFX_SUCCESS;
}

void jfx_frontend_shutdown(jfx_frontend_t *frontend) {
    if (!frontend) {
        return;
    }
    if (frontend->ops && frontend->ops->shutdown) {
        frontend->ops->shutdown(frontend->state);
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frontend);
}

void jfx_frontend_run(jfx_frontend_t *frontend) {
    if (frontend && frontend->ops && frontend->ops->run) {
        frontend->ops->run(frontend->state);
    }
}

jfx_result_t jfx_frontend_open_project(jfx_frontend_t *frontend, const char *path) {
    if (!frontend || !frontend->ops || !path || !path[0]) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->open_project) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->open_project(frontend->state, path);
}

jfx_result_t jfx_frontend_save_project(jfx_frontend_t *frontend, const char *path) {
    if (!frontend || !frontend->ops || !path || !path[0]) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->save_project) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->save_project(frontend->state, path);
}

jfx_result_t jfx_frontend_close_project(jfx_frontend_t *frontend) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->close_project) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->close_project(frontend->state);
}

jfx_result_t jfx_frontend_play(jfx_frontend_t *frontend) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->play) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->play(frontend->state);
}

jfx_result_t jfx_frontend_pause(jfx_frontend_t *frontend) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->pause) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->pause(frontend->state);
}

jfx_result_t jfx_frontend_seek(jfx_frontend_t *frontend, double time_seconds) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->seek) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->seek(frontend->state, time_seconds);
}

jfx_result_t jfx_frontend_set_loop(jfx_frontend_t *frontend, bool loop) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->set_loop) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->set_loop(frontend->state, loop);
}

jfx_result_t jfx_frontend_render_frame(jfx_frontend_t *frontend, uint32_t width,
    uint32_t height, uint8_t *out_rgba, size_t out_size) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!width || !height || !out_rgba) return JFX_ERROR_INVALID_ARGUMENT;
    if ((size_t)width > SIZE_MAX / (size_t)height / 4u) return JFX_ERROR_OUT_OF_MEMORY;
    if (out_size < (size_t)width * (size_t)height * 4u) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->render_frame) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->render_frame(frontend->state, width, height, out_rgba, out_size);
}

jfx_result_t jfx_frontend_resize_viewport(jfx_frontend_t *frontend, uint32_t width,
    uint32_t height) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->resize_viewport) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->resize_viewport(frontend->state, width, height);
}

jfx_result_t jfx_frontend_export_frames(jfx_frontend_t *frontend,
    const char *output_directory, uint32_t width, uint32_t height, uint32_t start_frame,
    uint32_t end_frame, jfx_export_progress_fn progress_cb, void *user_data) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!output_directory || !output_directory[0]) return JFX_ERROR_INVALID_ARGUMENT;
    if (!width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    if (end_frame < start_frame) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->export_frames) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->export_frames(frontend->state, output_directory, width, height,
        start_frame, end_frame, progress_cb, user_data);
}

jfx_result_t jfx_frontend_get_selection(jfx_frontend_t *frontend, char *out_buffer,
    size_t buffer_size) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!out_buffer || !buffer_size) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->get_selection) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->get_selection(frontend->state, out_buffer, buffer_size);
}

jfx_result_t jfx_frontend_set_selection(jfx_frontend_t *frontend, const char *selection) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!selection) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->set_selection) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->set_selection(frontend->state, selection);
}

jfx_result_t jfx_frontend_get_viewport_state(jfx_frontend_t *frontend,
    jfx_viewport_state_t *out_state) {
    if (!frontend || !frontend->ops) return JFX_ERROR_INVALID_ARGUMENT;
    if (!out_state) return JFX_ERROR_INVALID_ARGUMENT;
    if (!frontend->ops->get_viewport_state) return JFX_ERROR_NOT_IMPLEMENTED;
    return frontend->ops->get_viewport_state(frontend->state, out_state);
}
