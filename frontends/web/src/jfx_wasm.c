#include "jfx/web_session.h"

jfx_result_t jfx_web_session_export_begin(jfx_web_session_t *s,const char *path,const char *codec,
    uint32_t start,uint32_t count,bool audio,jfx_export_job_t **out) {
    jfx_export_options_t o={.size=sizeof(o),.path=path,.video_codec=codec && *codec?codec:NULL,
        .start_frame=start,.frame_count=count,.audio=audio};
    return jfx_export_begin(jfx_web_session_editor(s),&o,out);
}
uint32_t jfx_web_export_completed(const jfx_export_job_t *job) { return (uint32_t)jfx_export_completed_frames(job); }
jfx_result_t jfx_web_session_audio_mixer(jfx_web_session_t *s,uint32_t rate,jfx_audio_mixer_t **out) {
    return jfx_audio_mixer_create(jfx_editor_timeline(jfx_web_session_editor(s)),rate,out);
}
jfx_result_t jfx_web_audio_mixer_render(jfx_audio_mixer_t *m,double sample,uint32_t frames,float *out,size_t capacity) {
    if (!(sample>=0 && sample<=1.e12) || (double)(uint64_t)sample!=sample) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_audio_mixer_render(m,(uint64_t)sample,frames,out,capacity);
}
jfx_result_t jfx_web_session_render_audio(jfx_web_session_t *s,double sample,uint32_t rate,uint32_t frames,float *out,size_t capacity) {
    if (!(sample>=0 && sample<=1.e12) || (double)(uint64_t)sample!=sample) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_timeline_render_audio(jfx_editor_timeline(jfx_web_session_editor(s)),(uint64_t)sample,rate,frames,out,capacity);
}

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "joltscript/effects.h"
#include "tilly/allocator.h"

struct jfx_web_session {
    jfx_engine_t *engine;
    jfx_editor_t *editor;
    bool editing;
    jolt_effects_t *effects;
    double time_seconds;
    char effect_name[32];
    float parameter;
    int has_parameter;
};

static float clamp_unit(float value) {
    return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

static int effect_exists(const char *name) {
    for (size_t i = 0; jolt_effects_name(i); ++i) {
        if (strcmp(jolt_effects_name(i), name) == 0) return 1;
    }
    return 0;
}

jfx_result_t jfx_web_session_create(const char *backend_name,
    jfx_web_session_t **out_session) {
    if (!out_session) return JFX_ERROR_INVALID_ARGUMENT;
    *out_session = NULL;
    jfx_web_session_t *session = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),
        sizeof(*session), _Alignof(jfx_web_session_t));
    if (!session) return JFX_ERROR_OUT_OF_MEMORY;
    memset(session, 0, sizeof(*session));
    jfx_engine_config_t config = { .max_buffers = 2, .max_textures = 16,
        .max_kernels = 32, .backend_name = backend_name ? backend_name : "webgpu" };
    jfx_result_t result = jfx_engine_init(&config, &session->engine);
    if (result != JFX_SUCCESS) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), session);
        return result;
    }
    session->effects = jolt_effects_create();
    if (!session->effects) {
        jfx_engine_shutdown(session->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), session);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    snprintf(session->effect_name, sizeof(session->effect_name), "%s", "brightness");
    session->parameter = 0.0f;
    session->has_parameter = 1;
    session->editor = jfx_editor_create(320, 180);
    if (!session->editor) { jfx_web_session_destroy(session); return JFX_ERROR_OUT_OF_MEMORY; }
    *out_session = session;
    return JFX_SUCCESS;
}

void jfx_web_session_destroy(jfx_web_session_t *session) {
    if (!session) return;
    jfx_editor_destroy(session->editor);
    jolt_effects_destroy(session->effects);
    jfx_engine_shutdown(session->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), session);
}

jfx_result_t jfx_web_session_render(jfx_web_session_t *session, double time_seconds) {
    if (!session || !isfinite(time_seconds) || time_seconds < 0.0) return JFX_ERROR_INVALID_ARGUMENT;
    session->time_seconds = time_seconds;
    return jfx_engine_tick(session->engine);
}

jfx_result_t jfx_web_session_set_effect(jfx_web_session_t *session,
    const char *effect_name, float parameter) {
    if (!session || !effect_name || !effect_name[0] || !isfinite(parameter) ||
        strlen(effect_name) >= sizeof(session->effect_name) || !effect_exists(effect_name)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    float probe_input[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    float probe_output[4];
    jolt_status_t status = jolt_effects_apply(session->effects, effect_name, probe_input, 1,
        &parameter, 1, sizeof(probe_output), probe_output);
    session->has_parameter = status == JOLT_OK;
    if (!session->has_parameter) {
        status = jolt_effects_apply(session->effects, effect_name, probe_input, 1, NULL, 0,
            sizeof(probe_output), probe_output);
    }
    if (status != JOLT_OK) return JFX_ERROR_INVALID_ARGUMENT;
    snprintf(session->effect_name, sizeof(session->effect_name), "%s", effect_name);
    session->parameter = parameter;
    return JFX_SUCCESS;
}

jfx_result_t jfx_web_session_render_rgba(jfx_web_session_t *session,
    double time_seconds, uint32_t width, uint32_t height, uint8_t *out_rgba,
    size_t out_size) {
    if (!session || !width || !height || !out_rgba || !isfinite(time_seconds) ||
        time_seconds < 0.0 || (size_t)width > SIZE_MAX / (size_t)height / 4u ||
        out_size < (size_t)width * (size_t)height * 4u) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (session->editing) return jfx_editor_render(session->editor, time_seconds, width, height, out_rgba, out_size);
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / (4u * sizeof(float))) return JFX_ERROR_OUT_OF_MEMORY;
    size_t float_bytes = pixels * 4u * sizeof(float);
    float *input = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), float_bytes,
        _Alignof(float));
    float *output = tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), float_bytes,
        _Alignof(float));
    if (!input || !output) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), input);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), output);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    float phase = (float)(time_seconds - floor(time_seconds));
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            size_t index = ((size_t)y * width + x) * 4u;
            input[index] = (float)x / (float)(width > 1u ? width - 1u : 1u);
            input[index + 1u] = (float)y / (float)(height > 1u ? height - 1u : 1u);
            input[index + 2u] = phase;
            input[index + 3u] = 1.0f;
        }
    }
    const float *parameter = session->has_parameter ? &session->parameter : NULL;
    size_t parameter_count = session->has_parameter ? 1u : 0u;
    jolt_status_t status = jolt_effects_apply(session->effects, session->effect_name, input,
        pixels, parameter, parameter_count, float_bytes, output);
    if (status == JOLT_OK) {
        for (size_t i = 0; i < pixels * 4u; ++i) {
            out_rgba[i] = (uint8_t)lrintf(clamp_unit(output[i]) * 255.0f);
        }
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), input);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), output);
    if (status != JOLT_OK) return JFX_ERROR_BACKEND_FAILURE;
    return jfx_web_session_render(session, time_seconds);
}

/* Enabling the editor switches this surface's preview to its active document. */
jfx_editor_t *jfx_web_session_editor(jfx_web_session_t *session) {
    if (!session) return NULL;
    session->editing = true;
    return session->editor;
}
jfx_result_t jfx_web_session_load_document(jfx_web_session_t *session, const char *text, size_t length,
    char *out_error, size_t error_size) {
    if (!session) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_result_t r = jfx_editor_load(session->editor, text, length, out_error, error_size);
    if (r == JFX_SUCCESS) session->editing = true;
    return r;
}
jfx_result_t jfx_web_session_save_document(jfx_web_session_t *session, char *out_text, size_t capacity, size_t *out_written) {
    return session ? jfx_editor_save(session->editor, out_text, capacity, out_written) : JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_web_session_save_sequence(jfx_web_session_t *session,char *out,size_t cap,size_t *written) {
    return session?jfx_project_save_sequence(jfx_editor_timeline(session->editor),out,cap,written):JFX_ERROR_INVALID_ARGUMENT;
}

jfx_result_t jfx_web_session_edit(jfx_web_session_t *session, const char *op, uint32_t a, uint32_t b,
    uint32_t c, double value, const char *text) {
    if (!session) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_result_t r=jfx_editor_command(session->editor,op,a,b,c,value,text);
    if (r==JFX_SUCCESS) session->editing=true;
    return r;
}
jfx_result_t jfx_web_session_sequence_state(jfx_web_session_t *session,char *out,size_t capacity) {
    return session?jfx_editor_sequence_state(session->editor,out,capacity):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_web_session_render_frame(jfx_web_session_t *session,uint32_t frame,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    return session?jfx_editor_render_frame(session->editor,frame,w,h,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_web_session_graph_state(jfx_web_session_t *s,char *out,size_t cap) {
    return s?jfx_editor_graph_state(s->editor,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
jfx_result_t jfx_web_session_render_graph(jfx_web_session_t *s,uint32_t node,double seconds,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    return s?jfx_editor_render_graph(s->editor,node,seconds,w,h,out,cap):JFX_ERROR_INVALID_ARGUMENT;
}
