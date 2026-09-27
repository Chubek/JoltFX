/* Headless frontend: the shared frontend contract with no window and no UI.
 *
 * Implements playback, a real RGBA8 render through the engine's selected
 * backend, and a PPM frame-sequence export. The CLI drives it, so `joltfx
 * render` and `joltfx export` run the same path a GUI frontend does rather than
 * a private copy of it. */

#include "headless_frontend.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "joltscript/effects.h"
#include "tilly/allocator.h"

#define HEADLESS_DEFAULT_DURATION 10.0
#define HEADLESS_MAX_DIMENSION 8192u
#define HEADLESS_EFFECT_NAME_MAX 32

struct headless_state {
    jfx_engine_t *engine;
    jolt_effects_t *effects;
    uint32_t width;
    uint32_t height;
    double duration_seconds;
    double time_seconds;
    bool playing;
    bool looping;
    char effect_name[HEADLESS_EFFECT_NAME_MAX];
    float effect_parameter;
    bool has_parameter;
};

static void *headless_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static uint8_t to_unorm8(float value) {
    if (!(value > 0.0f)) return 0u; /* also catches NaN */
    if (value >= 1.0f) return 255u;
    return (uint8_t)lrintf(value * 255.0f);
}

jfx_result_t jfx_headless_set_effect(void *state, const char *effect_name, float parameter) {
    if (!state || !effect_name || !effect_name[0] || !isfinite(parameter)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    struct headless_state *headless = (struct headless_state *)state;
    if (strlen(effect_name) >= sizeof(headless->effect_name) ||
        !jolt_effects_exists(headless->effects, effect_name)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    headless->has_parameter =
        jolt_effects_parameter_count(headless->effects, effect_name) > 0;
    if (headless->has_parameter) {
        float lo = 0.0f;
        float hi = 0.0f;
        jolt_effects_parameter_range(headless->effects, effect_name, &lo, &hi);
        headless->effect_parameter =
            parameter < lo ? lo : (parameter > hi ? hi : parameter);
    } else {
        headless->effect_parameter = 0.0f;
    }
    snprintf(headless->effect_name, sizeof(headless->effect_name), "%s", effect_name);
    return JFX_SUCCESS;
}

/* Builds the animated gradient the effect is previewed on. */
static void build_input(const struct headless_state *headless, size_t pixels, float *input) {
    const double phase = headless->time_seconds - floor(headless->time_seconds);
    for (uint32_t y = 0; y < headless->height; ++y) {
        for (uint32_t x = 0; x < headless->width; ++x) {
            const size_t index = ((size_t)y * headless->width + x) * 4u;
            input[index] = (float)x / (float)(headless->width > 1u ? headless->width - 1u : 1u);
            input[index + 1u] = (float)y / (float)(headless->height > 1u ? headless->height - 1u : 1u);
            input[index + 2u] = (float)phase;
            input[index + 3u] = 1.0f;
        }
    }
    (void)pixels;
}

static jfx_result_t headless_init(const jfx_frontend_desc_t *desc, void **out_state) {
    if (!desc || !out_state) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_state = NULL;
    if (desc->size < sizeof(*desc)) {
        return JFX_ERROR_VERSION_MISMATCH;
    }
    struct headless_state *headless = headless_alloc(sizeof(*headless));
    if (!headless) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memset(headless, 0, sizeof(*headless));
    headless->width = desc->width ? desc->width : 64u;
    headless->height = desc->height ? desc->height : 64u;
    headless->duration_seconds = HEADLESS_DEFAULT_DURATION;
    headless->looping = true;
    if (headless->width > HEADLESS_MAX_DIMENSION || headless->height > HEADLESS_MAX_DIMENSION) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), headless);
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    jfx_engine_config_t engine_config;
    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.max_buffers = 256;
    engine_config.max_textures = 64;
    engine_config.max_kernels = 32;
    engine_config.backend_name = desc->backend_name;
    jfx_result_t status = jfx_engine_init(&engine_config, &headless->engine);
    if (status != JFX_SUCCESS) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), headless);
        return status;
    }
    headless->effects = jolt_effects_create();
    if (!headless->effects) {
        jfx_engine_shutdown(headless->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), headless);
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    status = jfx_headless_set_effect(headless, "brightness", 0.0f);
    if (status != JFX_SUCCESS) {
        jolt_effects_destroy(headless->effects);
        jfx_engine_shutdown(headless->engine);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), headless);
        return status;
    }
    *out_state = headless;
    return JFX_SUCCESS;
}

static void headless_shutdown(void *state) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless) {
        return;
    }
    jolt_effects_destroy(headless->effects);
    jfx_engine_shutdown(headless->engine);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), headless);
}

static jfx_result_t headless_play(void *state) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless) return JFX_ERROR_INVALID_ARGUMENT;
    headless->playing = true;
    return JFX_SUCCESS;
}

static jfx_result_t headless_pause(void *state) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless) return JFX_ERROR_INVALID_ARGUMENT;
    headless->playing = false;
    return JFX_SUCCESS;
}

static jfx_result_t headless_seek(void *state, double time_seconds) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless || !isfinite(time_seconds) || time_seconds < 0.0) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    headless->time_seconds = time_seconds;
    return JFX_SUCCESS;
}

static jfx_result_t headless_set_loop(void *state, bool loop) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless) return JFX_ERROR_INVALID_ARGUMENT;
    headless->looping = loop;
    return JFX_SUCCESS;
}

static jfx_result_t headless_resize(void *state, uint32_t width, uint32_t height) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless || !width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    if (width > HEADLESS_MAX_DIMENSION || height > HEADLESS_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    headless->width = width;
    headless->height = height;
    return JFX_SUCCESS;
}

static jfx_result_t headless_render_frame(void *state, uint32_t width, uint32_t height,
    uint8_t *out_rgba, size_t out_size) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless || !width || !height || !out_rgba) return JFX_ERROR_INVALID_ARGUMENT;
    if (width > HEADLESS_MAX_DIMENSION || height > HEADLESS_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    size_t pixels = (size_t)width * (size_t)height;
    if (pixels > SIZE_MAX / 4u) return JFX_ERROR_OUT_OF_MEMORY;
    if (out_size < pixels * 4u) return JFX_ERROR_INVALID_ARGUMENT;
    if (pixels > SIZE_MAX / (4u * sizeof(float))) return JFX_ERROR_OUT_OF_MEMORY;

    size_t bytecode_size = 0;
    const uint8_t *bytecode =
        jolt_effects_bytecode(headless->effects, headless->effect_name, &bytecode_size);
    if (!bytecode || !bytecode_size) return JFX_ERROR_NOT_FOUND;

    const size_t float_bytes = pixels * 4u * sizeof(float);
    float *input = headless_alloc(float_bytes);
    float *output = headless_alloc(float_bytes);
    if (!input || !output) {
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), input);
        tilly_free((tilly_allocator_t *)tilly_default_allocator(), output);
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    const uint32_t saved_width = headless->width;
    const uint32_t saved_height = headless->height;
    headless->width = width;
    headless->height = height;
    build_input(headless, pixels, input);
    headless->width = saved_width;
    headless->height = saved_height;

    const float *parameters = headless->has_parameter ? &headless->effect_parameter : NULL;
    const size_t parameter_count = headless->has_parameter ? 1u : 0u;
    jfx_result_t status = jfx_engine_execute_bytecode(headless->engine, bytecode,
        bytecode_size, input, pixels, parameters, parameter_count, output);
    if (status == JFX_SUCCESS) {
        for (size_t i = 0; i < pixels * 4u; ++i) {
            out_rgba[i] = to_unorm8(output[i]);
        }
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), input);
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), output);
    return status;
}

static jfx_result_t headless_get_viewport_state(void *state, jfx_viewport_state_t *out_state) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless || !out_state) return JFX_ERROR_INVALID_ARGUMENT;
    out_state->width = headless->width;
    out_state->height = headless->height;
    out_state->time_seconds = headless->time_seconds;
    out_state->duration_seconds = headless->duration_seconds;
    out_state->playing = headless->playing;
    out_state->looping = headless->looping;
    return JFX_SUCCESS;
}

/* Writes one binary PPM (P6) per frame. */
static jfx_result_t write_ppm(const char *path, const uint8_t *rgba, uint32_t width,
    uint32_t height) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return JFX_ERROR_NOT_FOUND;
    }
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        fclose(file);
        return JFX_ERROR_BACKEND_FAILURE;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = rgba + (size_t)y * width * 4u;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t rgb[3] = { row[x * 4u], row[x * 4u + 1u], row[x * 4u + 2u] };
            if (fwrite(rgb, 1, 3, file) != 3) {
                fclose(file);
                return JFX_ERROR_BACKEND_FAILURE;
            }
        }
    }
    return fclose(file) == 0 ? JFX_SUCCESS : JFX_ERROR_BACKEND_FAILURE;
}

static uint64_t monotonic_ns(void) {
    struct timespec timestamp;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &timestamp) != 0) {
        return 0;
    }
#else
    if (timespec_get(&timestamp, TIME_UTC) != TIME_UTC) {
        return 0;
    }
#endif
    return (uint64_t)timestamp.tv_sec * UINT64_C(1000000000) + (uint64_t)timestamp.tv_nsec;
}

static jfx_result_t headless_export_frames(void *state, const char *output_directory,
    uint32_t width, uint32_t height, uint32_t start_frame, uint32_t end_frame,
    jfx_export_progress_fn progress_cb, void *user_data) {
    struct headless_state *headless = (struct headless_state *)state;
    if (!headless || !output_directory || !output_directory[0]) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!width || !height || end_frame < start_frame) return JFX_ERROR_INVALID_ARGUMENT;
    if (width > HEADLESS_MAX_DIMENSION || height > HEADLESS_MAX_DIMENSION) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t total = (size_t)end_frame - start_frame + 1u;
    const size_t frame_bytes = (size_t)width * height * 4u;
    uint8_t *frame = headless_alloc(frame_bytes);
    if (!frame) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }

    const uint64_t started_ns = monotonic_ns();
    /* The default export cadence. A project file would carry its own frame
     * rate; the headless frontend has no project to read one from. */
    const double fps = 24.0;
    jfx_result_t status = JFX_SUCCESS;
    for (size_t index = 0; index < total; ++index) {
        const uint32_t frame_number = start_frame + (uint32_t)index;
        const double time_seconds = (double)frame_number / fps;
        status = headless_seek(headless, time_seconds);
        if (status != JFX_SUCCESS) {
            break;
        }
        status = headless_render_frame(headless, width, height, frame, frame_bytes);
        if (status != JFX_SUCCESS) {
            break;
        }
        char path[1024];
        snprintf(path, sizeof(path), "%s/frame_%06u.ppm", output_directory, frame_number);
        status = write_ppm(path, frame, width, height);
        if (status != JFX_SUCCESS) {
            break;
        }
        if (progress_cb) {
            const uint64_t now_ns = monotonic_ns();
            const double elapsed = now_ns > started_ns
                ? (double)(now_ns - started_ns) / 1.0e9
                : 0.0;
            if (progress_cb(index + 1u, total, elapsed, user_data) != 0) {
                /* Cancelled by the caller: what was written stays written. */
                break;
            }
        }
    }
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), frame);
    return status;
}

const char *jfx_headless_backend_name(const jfx_frontend_t *frontend) {
    if (!frontend) {
        return NULL;
    }
    const struct headless_state *headless = (const struct headless_state *)frontend->state;
    return headless ? jfx_engine_backend_name(headless->engine) : NULL;
}

const jfx_frontend_ops_t jfx_headless_frontend_ops = {
    .init = headless_init,
    .shutdown = headless_shutdown,
    .run = NULL, /* no loop: a headless frontend is driven from outside */
    .open_project = NULL,
    .save_project = NULL,
    .close_project = NULL,
    .play = headless_play,
    .pause = headless_pause,
    .seek = headless_seek,
    .set_loop = headless_set_loop,
    .render_frame = headless_render_frame,
    .resize_viewport = headless_resize,
    .export_frames = headless_export_frames,
    .get_selection = NULL,
    .set_selection = NULL,
    .get_viewport_state = headless_get_viewport_state,
};
