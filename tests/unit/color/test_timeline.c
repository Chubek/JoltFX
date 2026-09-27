/* Timeline conformance.
 *
 * The timeline is the NLE model: tracks, clips, in/out points, the per-clip
 * effect stack that the layer-based panel edits, and keyframes. The properties
 * that matter are that time arithmetic is exact and frame-based (no float
 * drift, and a clip covers exactly the frames it says it does), that structural
 * edits keep indices consistent, and that a keyframed parameter evaluates to the
 * value the interpolation says it should.
 */

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jfx/jfx_image.h"
#include "jfx/jfx_timeline.h"

#define W 8u
#define H 4u
#define PX (W * H)

static uint8_t g_frame[PX * 4];

static float ch(const uint8_t *frame, size_t index, int channel) {
    return (float)frame[index * 4u + (size_t)channel] / 255.0f;
}

static void near(float actual, float expected, float tolerance, const char *what) {
    if (fabsf(actual - expected) > tolerance) {
        fprintf(stderr, "FAIL %s: got %.4f, want %.4f\n", what, (double)actual, (double)expected);
        assert(fabsf(actual - expected) <= tolerance);
    }
}

static jfx_clip_desc_t solid_desc(const char *name, float r, float g, float b, float a,
    uint64_t start, uint64_t length) {
    jfx_clip_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.name = name;
    desc.source = JFX_CLIP_SOLID;
    desc.source_params[0] = r;
    desc.source_params[1] = g;
    desc.source_params[2] = b;
    desc.source_params[3] = a;
    desc.start_frame = start;
    desc.length_frames = length;
    desc.in_point = 0;
    desc.opacity = 1.0f;
    desc.blend_mode = JFX_BLEND_NORMAL;
    desc.enabled = true;
    return desc;
}

static void test_construction(void) {
    assert(jfx_timeline_create(0, 100, 30, 1) == NULL);
    assert(jfx_timeline_create(100, 0, 30, 1) == NULL);
    assert(jfx_timeline_create(100, 100, 0, 1) == NULL);
    assert(jfx_timeline_create(100, 100, 30, 0) == NULL);
    jfx_timeline_destroy(NULL);

    jfx_timeline_t *timeline = jfx_timeline_create(1920, 1080, 30000, 1001);
    assert(timeline);
    assert(jfx_timeline_width(timeline) == 1920);
    assert(jfx_timeline_height(timeline) == 1080);
    /* An exact rational rate, not the rounded decimal. */
    near((float)jfx_timeline_fps(timeline), 29.97f, 0.005f, "ntsc rate");
    assert(jfx_timeline_track_count(timeline) == 0u);
    assert(jfx_timeline_duration(timeline) == 0u);

    char timecode[32];
    assert(jfx_timeline_timecode(timeline, 0, timecode, sizeof(timecode)) == JFX_SUCCESS);
    assert(strcmp(timecode, "00:00:00:00") == 0);
    assert(jfx_timeline_timecode(timeline, 29, timecode, sizeof(timecode)) == JFX_SUCCESS);
    assert(strcmp(timecode, "00:00:00:29") == 0);
    assert(jfx_timeline_timecode(timeline, 30, timecode, sizeof(timecode)) == JFX_SUCCESS);
    assert(strcmp(timecode, "00:00:01:00") == 0);
    assert(jfx_timeline_timecode(timeline, 90, timecode, sizeof(timecode)) == JFX_SUCCESS);
    assert(strcmp(timecode, "00:00:03:00") == 0);
    assert(jfx_timeline_timecode(timeline, 90, timecode, 4) == JFX_ERROR_BACKEND_FAILURE);
    assert(jfx_timeline_timecode(timeline, 0, NULL, 8) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);

    /* Source names round-trip, which a CLI and a project file both rely on. */
    for (int i = 0; i < JFX_CLIP_SOURCE_COUNT; ++i) {
        const char *name = jfx_clip_source_name((jfx_clip_source_t)i);
        assert(name);
        assert(jfx_clip_source_parse(name) == (jfx_clip_source_t)i);
    }
    assert(jfx_clip_source_parse("colour") == JFX_CLIP_SOURCE_COUNT);
    assert(jfx_clip_source_parse(NULL) == JFX_CLIP_SOURCE_COUNT);
}

static void test_tracks(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    assert(jfx_timeline_add_track(timeline, "Lower") == 0u);
    assert(jfx_timeline_add_track(timeline, "Upper") == 1u);
    assert(strcmp(jfx_timeline_track_name(timeline, 1), "Upper") == 0);
    assert(jfx_timeline_set_track_name(timeline, 1, "Top") == JFX_SUCCESS);
    assert(strcmp(jfx_timeline_track_name(timeline, 1), "Top") == 0);
    /* Out-of-range access is refused rather than reading past the array. */
    assert(jfx_timeline_track_name(timeline, 9) == NULL);
    assert(jfx_timeline_set_track_name(timeline, 9, "x") == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_track_muted(timeline, 0, true) == JFX_SUCCESS);
    assert(jfx_timeline_track_muted(timeline, 0));
    assert(!jfx_timeline_track_muted(timeline, 1));
    assert(jfx_timeline_set_track_solo(timeline, 1, true) == JFX_SUCCESS);
    assert(jfx_timeline_track_solo(timeline, 1));
    assert(jfx_timeline_set_track_opacity(timeline, 0, 0.5f) == JFX_SUCCESS);
    near(jfx_timeline_track_opacity(timeline, 0), 0.5f, 0.0f, "track opacity");
    assert(jfx_timeline_set_track_opacity(timeline, 0, NAN) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_track_blend(timeline, 0, JFX_BLEND_SCREEN) == JFX_SUCCESS);
    assert(jfx_timeline_track_blend(timeline, 0) == JFX_BLEND_SCREEN);
    assert(jfx_timeline_set_track_blend(timeline, 0, (jfx_blend_mode_t)99) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* Removing shifts the survivors down, keeping creation order. */
    assert(jfx_timeline_remove_track(timeline, 0) == JFX_SUCCESS);
    assert(jfx_timeline_track_count(timeline) == 1u);
    assert(strcmp(jfx_timeline_track_name(timeline, 0), "Top") == 0);
    assert(jfx_timeline_remove_track(timeline, 9) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);
}

static void test_clip_placement(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t track = jfx_timeline_add_track(timeline, "V1");
    jfx_clip_desc_t desc = solid_desc("A", 1, 0, 0, 1, 10, 5);
    const uint32_t clip = jfx_timeline_add_clip(timeline, track, &desc);
    assert(clip == 0u);
    assert(jfx_timeline_clip_count(timeline, track) == 1u);
    assert(jfx_timeline_clip_start(timeline, track, clip) == 10u);
    assert(jfx_timeline_clip_length(timeline, track, clip) == 5u);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, clip), "A") == 0);

    /* A clip covers [start, start+length) exactly - the frame at its far edge is
     * the next clip's, not its own. */
    assert(!jfx_timeline_clip_covers(timeline, track, clip, 9));
    for (uint64_t f = 10; f < 15; ++f) {
        assert(jfx_timeline_clip_covers(timeline, track, clip, f));
    }
    assert(!jfx_timeline_clip_covers(timeline, track, clip, 15));
    assert(jfx_timeline_clip_at(timeline, track, 12) == 0);
    assert(jfx_timeline_clip_at(timeline, track, 15) == -1);
    assert(jfx_timeline_clip_at(timeline, 9, 12) == -1);
    assert(jfx_timeline_duration(timeline) == 15u);

    /* A zero-length clip is rejected: it would cover no frames at all. */
    desc.length_frames = 0;
    assert(jfx_timeline_add_clip(timeline, track, &desc) == UINT32_MAX);
    desc.length_frames = 5;
    /* An image clip needs a path. */
    desc.source = JFX_CLIP_IMAGE;
    desc.image_path = NULL;
    assert(jfx_timeline_add_clip(timeline, track, &desc) == UINT32_MAX);
    desc.image_path = "/nonexistent.png";
    assert(jfx_timeline_add_clip(timeline, track, &desc) != UINT32_MAX);
    assert(jfx_timeline_clip_count(timeline, track) == 2u);
    desc.source = JFX_CLIP_SOLID;
    assert(jfx_timeline_add_clip(NULL, track, &desc) == UINT32_MAX);
    assert(jfx_timeline_add_clip(timeline, 9, &desc) == UINT32_MAX);
    assert(jfx_timeline_add_clip(timeline, track, NULL) == UINT32_MAX);

    /* Reordering is a drag in the timeline. Three clips with distinct names,
     * because a reorder that shifts the wrong range drops a clip entirely and
     * two identically named clips would hide it. */
    jfx_clip_desc_t c = solid_desc("C", 0, 0, 1, 1, 20, 5);
    jfx_clip_desc_t d = solid_desc("D", 1, 1, 0, 1, 30, 5);
    assert(jfx_timeline_add_clip(timeline, track, &c) == 2u);
    assert(jfx_timeline_add_clip(timeline, track, &d) == 3u);
    assert(jfx_timeline_clip_count(timeline, track) == 4u);

    /* Later to earlier: the run from the target up to the clip shifts up. */
    assert(jfx_timeline_move_clip(timeline, track, 3, 0) == JFX_SUCCESS);
    assert(jfx_timeline_clip_count(timeline, track) == 4u);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 0), "D") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 1), "A") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 2), "A") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 3), "C") == 0);

    /* Earlier to later: everything between shifts down. */
    assert(jfx_timeline_move_clip(timeline, track, 0, 3) == JFX_SUCCESS);
    assert(jfx_timeline_clip_count(timeline, track) == 4u);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 0), "A") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 1), "A") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 2), "C") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 3), "D") == 0);

    /* A move to the same place is a no-op, and out-of-range is refused. */
    assert(jfx_timeline_move_clip(timeline, track, 1, 1) == JFX_SUCCESS);
    assert(jfx_timeline_move_clip(timeline, track, 0, 9) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_move_clip(timeline, track, 9, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_clip_count(timeline, track) == 4u);

    /* Trimming the head moves the source window with it, so the same frames
     * stay visible. */
    assert(jfx_timeline_trim_clip(timeline, track, 0, 12, 3) == JFX_SUCCESS);
    assert(jfx_timeline_clip_start(timeline, track, 0) == 12u);
    assert(jfx_timeline_clip_length(timeline, track, 0) == 3u);
    assert(jfx_timeline_clip_in_point(timeline, track, 0) == 2u);
    assert(jfx_timeline_trim_clip(timeline, track, 0, 0, 0) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_trim_clip(timeline, track, 0, -1, 4) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_trim_clip(timeline, track, 9, 0, 4) == JFX_ERROR_INVALID_ARGUMENT);

    /* Removal shifts down, so the survivors keep their relative order. */
    assert(jfx_timeline_remove_clip(timeline, track, 1) == JFX_SUCCESS);
    assert(jfx_timeline_clip_count(timeline, track) == 3u);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 0), "A") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 1), "C") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, track, 2), "D") == 0);
    assert(jfx_timeline_remove_clip(timeline, track, 5) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);
}

static void test_relocate(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t lower = jfx_timeline_add_track(timeline, "V1");
    const uint32_t upper = jfx_timeline_add_track(timeline, "V2");
    jfx_clip_desc_t a = solid_desc("A", 1, 0, 0, 1, 0, 4);
    jfx_clip_desc_t b = solid_desc("B", 0, 1, 0, 1, 0, 4);
    jfx_clip_desc_t c = solid_desc("C", 0, 0, 1, 1, 0, 4);
    assert(jfx_timeline_add_clip(timeline, lower, &a) == 0u);
    assert(jfx_timeline_add_clip(timeline, lower, &b) == 1u);
    assert(jfx_timeline_add_clip(timeline, upper, &c) == 0u);

    assert(jfx_timeline_relocate_clip(timeline, lower, 0, upper, 0, false) == JFX_SUCCESS);
    assert(jfx_timeline_clip_count(timeline, lower) == 1u);
    assert(jfx_timeline_clip_count(timeline, upper) == 2u);
    assert(strcmp(jfx_timeline_clip_name(timeline, upper, 0), "A") == 0);
    assert(strcmp(jfx_timeline_clip_name(timeline, upper, 1), "C") == 0);
    assert(jfx_timeline_relocate_clip(timeline, lower, 0, upper, 9, false) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);

    /* Moving a clip must transfer ownership of its path and effect strings, not
     * free them and reinsert the dangling pointers. A clip with a path is the
     * case that catches it. */
    jfx_timeline_t *owned = jfx_timeline_create(W, H, 30, 1);
    const uint32_t lo = jfx_timeline_add_track(owned, "V1");
    const uint32_t hi = jfx_timeline_add_track(owned, "V2");
    jfx_clip_desc_t pic = solid_desc("Pic", 1, 1, 1, 1, 0, 4);
    pic.source = JFX_CLIP_IMAGE;
    pic.image_path = "/nonexistent-frame.png";
    const uint32_t moved = jfx_timeline_add_clip(owned, lo, &pic);
    assert(moved == 0u);
    const uint32_t fx = jfx_timeline_add_effect(owned, lo, moved, "lut");
    assert(jfx_timeline_set_effect_string(owned, lo, moved, fx, 0, "/nonexistent.cube") ==
        JFX_SUCCESS);
    assert(jfx_timeline_relocate_clip(owned, lo, moved, hi, 0, false) == JFX_SUCCESS);
    assert(jfx_timeline_clip_count(owned, lo) == 0u);
    assert(jfx_timeline_clip_count(owned, hi) == 1u);
    /* The strings came with it and are still readable. */
    assert(strcmp(jfx_timeline_effect_string(owned, hi, 0, 0, 0), "/nonexistent.cube") == 0);
    jfx_timeline_destroy(owned);
}

/* An effect stack is the layer-based effects panel's model, so the panel's
 * operations - add, disable, reorder, set a blend - must all be expressible. */
static void test_effect_stack(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t track = jfx_timeline_add_track(timeline, "V1");
    jfx_clip_desc_t desc = solid_desc("A", 0.5f, 0.5f, 0.5f, 1.0f, 0, 4);
    const uint32_t clip = jfx_timeline_add_clip(timeline, track, &desc);

    assert(jfx_timeline_effect_count(timeline, track, clip) == 0u);
    const uint32_t exposure = jfx_timeline_add_effect(timeline, track, clip, "exposure");
    assert(exposure == 0u);
    const uint32_t contrast = jfx_timeline_add_effect(timeline, track, clip, "contrast");
    assert(contrast == 1u);
    assert(jfx_timeline_effect_count(timeline, track, clip) == 2u);
    assert(strcmp(jfx_timeline_effect_kind(timeline, track, clip, 0), "exposure") == 0);
    /* A panel builds its controls from the kind descriptor, so it must be there. */
    const jfx_node_kind_t *kind = jfx_timeline_effect_kind_desc(timeline, track, clip, 0);
    assert(kind && strcmp(kind->name, "exposure") == 0);
    assert(kind->param_count == 1u);

    /* A source or a two-input blend cannot be a single stack stage. */
    assert(jfx_timeline_add_effect(timeline, track, clip, "solid") == UINT32_MAX);
    assert(jfx_timeline_add_effect(timeline, track, clip, "blend") == UINT32_MAX);
    assert(jfx_timeline_add_effect(timeline, track, clip, "no-such-kind") == UINT32_MAX);
    assert(jfx_timeline_effect_count(timeline, track, clip) == 2u);

    /* Parameters start at their declared defaults and clamp into the range. */
    near(jfx_timeline_effect_param(timeline, track, clip, 0, 0), 0.0f, 0.0f, "exposure default");
    near(jfx_timeline_effect_param(timeline, track, clip, 1, 0), 1.0f, 0.0f, "contrast default");
    assert(jfx_timeline_set_effect_param(timeline, track, clip, 0, 0, 1.5f) == JFX_SUCCESS);
    near(jfx_timeline_effect_param(timeline, track, clip, 0, 0), 1.5f, 0.0f, "exposure set");
    assert(jfx_timeline_set_effect_param(timeline, track, clip, 0, 9, 1.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_effect_param(timeline, track, clip, 0, 0, NAN) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_set_effect_param(timeline, track, clip, 9, 0, 1.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* Per-effect state a panel needs. */
    assert(jfx_timeline_set_effect_enabled(timeline, track, clip, 0, false) == JFX_SUCCESS);
    assert(!jfx_timeline_effect_enabled(timeline, track, clip, 0));
    assert(jfx_timeline_effect_enabled(timeline, track, clip, 1));
    assert(jfx_timeline_set_effect_opacity(timeline, track, clip, 1, 0.25f) == JFX_SUCCESS);
    near(jfx_timeline_effect_opacity(timeline, track, clip, 1), 0.25f, 0.0f, "effect opacity");
    assert(jfx_timeline_set_effect_blend(timeline, track, clip, 1, JFX_BLEND_MULTIPLY) ==
        JFX_SUCCESS);
    assert(jfx_timeline_effect_blend(timeline, track, clip, 1) == JFX_BLEND_MULTIPLY);
    assert(jfx_timeline_set_effect_blend(timeline, track, clip, 1, (jfx_blend_mode_t)99) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* Reordering the stack is a drag in the panel. */
    assert(jfx_timeline_move_effect(timeline, track, clip, 0, 1) == JFX_SUCCESS);
    assert(strcmp(jfx_timeline_effect_kind(timeline, track, clip, 0), "contrast") == 0);
    assert(strcmp(jfx_timeline_effect_kind(timeline, track, clip, 1), "exposure") == 0);
    assert(jfx_timeline_move_effect(timeline, track, clip, 0, 9) == JFX_ERROR_INVALID_ARGUMENT);

    /* A LUT effect carries a path, which the grading panel sets. */
    const uint32_t lut = jfx_timeline_add_effect(timeline, track, clip, "lut");
    assert(lut == 2u);
    assert(jfx_timeline_set_effect_string(timeline, track, clip, lut, 0, "/tmp/look.cube") ==
        JFX_SUCCESS);
    assert(strcmp(jfx_timeline_effect_string(timeline, track, clip, lut, 0), "/tmp/look.cube") == 0);
    /* A kind with no string field refuses one. */
    assert(jfx_timeline_set_effect_string(timeline, track, clip, 0, 0, "x") ==
        JFX_ERROR_INVALID_ARGUMENT);
    /* Setting NULL clears the slot. */
    assert(jfx_timeline_set_effect_string(timeline, track, clip, lut, 0, NULL) == JFX_SUCCESS);
    assert(jfx_timeline_effect_string(timeline, track, clip, lut, 0) == NULL);

    assert(jfx_timeline_remove_effect(timeline, track, clip, lut) == JFX_SUCCESS);
    assert(jfx_timeline_effect_count(timeline, track, clip) == 2u);
    assert(jfx_timeline_remove_effect(timeline, track, clip, 9) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);
}

static void test_keyframes(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t track = jfx_timeline_add_track(timeline, "V1");
    jfx_clip_desc_t desc = solid_desc("A", 0.5f, 0.5f, 0.5f, 1.0f, 0, 100);
    const uint32_t clip = jfx_timeline_add_clip(timeline, track, &desc);
    const uint32_t effect = jfx_timeline_add_effect(timeline, track, clip, "opacity");

    /* With no keys the static value is the value at every frame. */
    assert(jfx_timeline_key_count(timeline, track, clip, effect, 0) == 0u);
    assert(jfx_timeline_set_effect_param(timeline, track, clip, effect, 0, 0.5f) == JFX_SUCCESS);
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 0), 0.5f, 0.0f, "no keys");
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 99), 0.5f, 0.0f, "no keys");

    /* Two keys, linear by default. */
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 0, 10, 0.0f) == JFX_SUCCESS);
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 0, 20, 1.0f) == JFX_SUCCESS);
    assert(jfx_timeline_key_count(timeline, track, clip, effect, 0) == 2u);
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 10), 0.0f, 0.001f, "k1");
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 15), 0.5f, 0.001f, "mid");
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 20), 1.0f, 0.001f, "k2");
    /* Outside the key range the value holds rather than extrapolating. */
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 0), 0.0f, 0.001f, "before");
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 100), 1.0f, 0.001f, "after");

    /* Hold: the left key's value until the right one. */
    assert(jfx_timeline_set_effect_interp(timeline, track, clip, effect, JFX_INTERP_HOLD) ==
        JFX_SUCCESS);
    assert(jfx_timeline_effect_interp(timeline, track, clip, effect) == JFX_INTERP_HOLD);
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 15), 0.0f, 0.001f, "hold");
    /* Smooth: eases in and out, so the midpoint is still the average. */
    assert(jfx_timeline_set_effect_interp(timeline, track, clip, effect, JFX_INTERP_SMOOTH) ==
        JFX_SUCCESS);
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 15), 0.5f, 0.001f,
        "smooth mid");
    const float quarter = jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 12);
    assert(quarter < 0.25f); /* smoothstep lags a linear ramp */
    assert(jfx_timeline_set_effect_interp(timeline, track, clip, effect, (jfx_interp_t)9) ==
        JFX_ERROR_INVALID_ARGUMENT);

    /* Adding a key on a frame that already has one edits it. */
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 0, 10, 0.5f) == JFX_SUCCESS);
    assert(jfx_timeline_key_count(timeline, track, clip, effect, 0) == 2u);
    float value = 0.0f;
    assert(jfx_timeline_key_at(timeline, track, clip, effect, 0, 10, &value));
    near(value, 0.5f, 0.0f, "edited key");

    /* Keys stay sorted however they are added. */
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 0, 15, 0.25f) == JFX_SUCCESS);
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 0, 5, 0.75f) == JFX_SUCCESS);
    assert(jfx_timeline_key_count(timeline, track, clip, effect, 0) == 4u);
    for (uint64_t f = 5; f <= 20; ++f) {
        (void)jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, f);
    }
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 5), 0.75f, 0.001f, "k@5");
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 10), 0.5f, 0.001f, "k@10");
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 15), 0.25f, 0.001f, "k@15");

    assert(jfx_timeline_remove_key(timeline, track, clip, effect, 0, 15) == JFX_SUCCESS);
    assert(jfx_timeline_key_count(timeline, track, clip, effect, 0) == 3u);
    assert(jfx_timeline_remove_key(timeline, track, clip, effect, 0, 15) ==
        JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_key_at(timeline, track, clip, effect, 0, 15, &value) == false);
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 0, 1000, 0.1f) == JFX_SUCCESS);
    assert(jfx_timeline_clear_keys(timeline, track, clip, effect, 0) == JFX_SUCCESS);
    assert(jfx_timeline_key_count(timeline, track, clip, effect, 0) == 0u);
    /* With the keys gone the static value is back. */
    near(jfx_timeline_effect_param_at(timeline, track, clip, effect, 0, 15), 0.5f, 0.0f,
        "cleared keys");
    assert(jfx_timeline_add_key(timeline, track, clip, 9, 0, 0, 0.0f) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_add_key(timeline, track, clip, effect, 9, 0, 0.0f) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);
}

static void test_render_tracks(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t lower = jfx_timeline_add_track(timeline, "V1");
    jfx_clip_desc_t red = solid_desc("Red", 1, 0, 0, 1, 0, 10);
    assert(jfx_timeline_add_clip(timeline, lower, &red) == 0u);

    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    for (size_t i = 0; i < PX; ++i) {
        near(ch(g_frame, i, 0), 1.0f, 0.0f, "red");
        near(ch(g_frame, i, 3), 1.0f, 0.0f, "red opaque");
    }

    /* An upper track composites over the lower one. */
    const uint32_t upper = jfx_timeline_add_track(timeline, "V2");
    jfx_clip_desc_t half = solid_desc("Half", 0, 0, 1, 0.5f, 0, 10);
    assert(jfx_timeline_add_clip(timeline, upper, &half) == 0u);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    for (size_t i = 0; i < PX; ++i) {
        near(ch(g_frame, i, 0), 0.5f, 0.01f, "blend red");
        near(ch(g_frame, i, 2), 0.5f, 0.01f, "blend blue");
        near(ch(g_frame, i, 3), 1.0f, 0.0f, "still opaque");
    }

    /* Mute hides a track; solo isolates one. */
    assert(jfx_timeline_set_track_muted(timeline, upper, true) == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 2), 0.0f, 0.0f, "muted upper");
    assert(jfx_timeline_set_track_muted(timeline, upper, false) == JFX_SUCCESS);
    assert(jfx_timeline_set_track_solo(timeline, lower, true) == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 2), 0.0f, 0.0f, "solo lower hides upper");
    assert(jfx_timeline_set_track_solo(timeline, lower, false) == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 2), 0.5f, 0.01f, "restored");

    /* A frame with no clip on any track is transparent, not black, so a layer
     * can fade up from clear. */
    assert(jfx_timeline_render(timeline, 50, 0.0f, g_frame) == JFX_SUCCESS);
    for (size_t i = 0; i < PX; ++i) {
        assert(g_frame[i * 4u + 3u] == 0u);
    }

    /* A disabled clip contributes nothing. */
    assert(jfx_timeline_set_clip_enabled(timeline, lower, 0, false) == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 2), 1.0f, 0.01f, "only the upper clip remains");
    assert(jfx_timeline_set_clip_enabled(timeline, lower, 0, true) == JFX_SUCCESS);

    /* An empty timeline still renders. */
    jfx_timeline_t *empty = jfx_timeline_create(W, H, 30, 1);
    assert(jfx_timeline_render(empty, 0, 0.0f, g_frame) == JFX_SUCCESS);
    assert(jfx_timeline_render(NULL, 0, 0.0f, g_frame) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_timeline_render(timeline, 0, 0.0f, NULL) == JFX_ERROR_INVALID_ARGUMENT);

    jfx_image_t image = { .size = sizeof(image) };
    assert(jfx_timeline_render_image(timeline, 0, 0.0f, &image) == JFX_SUCCESS);
    assert(image.width == W && image.height == H && image.channels == 4);
    assert(image.pixels);
    jfx_image_release(&image);
    jfx_image_t bad = { .size = 1 };
    assert(jfx_timeline_render_image(timeline, 0, 0.0f, &bad) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(empty);
    jfx_timeline_destroy(timeline);
}

/* A clip's effect stack has to actually change the pixels, in order, and honour
 * the disable and opacity flags. */
static void test_render_effects(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t track = jfx_timeline_add_track(timeline, "V1");
    jfx_clip_desc_t desc = solid_desc("Grey", 0.5f, 0.5f, 0.5f, 1.0f, 0, 10);
    const uint32_t clip = jfx_timeline_add_clip(timeline, track, &desc);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 0), 0.5f, 0.01f, "before any effect");

    /* One stop brighter: 0.5 -> 1.0. */
    const uint32_t exposure = jfx_timeline_add_effect(timeline, track, clip, "exposure");
    assert(jfx_timeline_set_effect_param(timeline, track, clip, exposure, 0, 1.0f) ==
        JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 0), 1.0f, 0.01f, "exposure applied");

    /* A disabled effect is skipped. */
    assert(jfx_timeline_set_effect_enabled(timeline, track, clip, exposure, false) ==
        JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 0), 0.5f, 0.01f, "disabled effect");
    assert(jfx_timeline_set_effect_enabled(timeline, track, clip, exposure, true) ==
        JFX_SUCCESS);

    /* Order matters: a second exposure after the first doubles again. */
    const uint32_t second = jfx_timeline_add_effect(timeline, track, clip, "exposure");
    assert(jfx_timeline_set_effect_param(timeline, track, clip, second, 0, 1.0f) == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 0), 1.0f, 0.01f, "clamped after two stops");
    assert(jfx_timeline_remove_effect(timeline, track, clip, second) == JFX_SUCCESS);

    /* A keyframed effect is evaluated at the rendered frame. */
    const uint32_t opacity = jfx_timeline_add_effect(timeline, track, clip, "opacity");
    assert(jfx_timeline_add_key(timeline, track, clip, opacity, 0, 0, 1.0f) == JFX_SUCCESS);
    assert(jfx_timeline_add_key(timeline, track, clip, opacity, 0, 4, 0.0f) == JFX_SUCCESS);
    assert(jfx_timeline_render(timeline, 0, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 3), 1.0f, 0.01f, "clip opaque at frame 0");
    assert(jfx_timeline_render(timeline, 4, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 3), 0.0f, 0.01f, "clip clear at frame 4");
    assert(jfx_timeline_render(timeline, 2, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 3), 0.5f, 0.02f, "clip half faded at frame 2");

    /* A clip's keyframes are relative to the clip, not the sequence. */
    jfx_timeline_t *later = jfx_timeline_create(W, H, 30, 1);
    const uint32_t t2 = jfx_timeline_add_track(later, "V1");
    jfx_clip_desc_t moved = solid_desc("Grey", 0.5f, 0.5f, 0.5f, 1.0f, 20, 10);
    const uint32_t c2 = jfx_timeline_add_clip(later, t2, &moved);
    const uint32_t o2 = jfx_timeline_add_effect(later, t2, c2, "opacity");
    assert(jfx_timeline_add_key(later, t2, c2, o2, 0, 0, 1.0f) == JFX_SUCCESS);
    assert(jfx_timeline_add_key(later, t2, c2, o2, 0, 9, 0.0f) == JFX_SUCCESS);
    assert(jfx_timeline_render(later, 20, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 3), 1.0f, 0.01f, "faded in at the clip's own start");
    assert(jfx_timeline_render(later, 29, 0.0f, g_frame) == JFX_SUCCESS);
    near(ch(g_frame, 0, 3), 0.0f, 0.01f, "faded out at the clip's own end");
    jfx_timeline_destroy(later);
    jfx_timeline_destroy(timeline);
}

static void test_describe(void) {
    jfx_timeline_t *timeline = jfx_timeline_create(W, H, 30, 1);
    const uint32_t track = jfx_timeline_add_track(timeline, "V1");
    jfx_clip_desc_t desc = solid_desc("A", 1, 1, 1, 1, 0, 10);
    assert(jfx_timeline_add_clip(timeline, track, &desc) == 0u);
    char text[64];
    assert(jfx_timeline_describe_track(timeline, track, text, sizeof(text)) == JFX_SUCCESS);
    assert(strstr(text, "1 clip") != NULL);
    assert(strstr(text, "00:00:00:10") != NULL);
    assert(jfx_timeline_describe_track(timeline, track, text, 4) == JFX_ERROR_BACKEND_FAILURE);
    assert(jfx_timeline_describe_track(timeline, 9, text, sizeof(text)) ==
        JFX_ERROR_INVALID_ARGUMENT);
    jfx_timeline_destroy(timeline);
}

int main(void) {
    test_construction();
    test_tracks();
    test_clip_placement();
    test_relocate();
    test_effect_stack();
    test_keyframes();
    test_render_tracks();
    test_render_effects();
    test_describe();
    puts("Timeline conformance tests passed");
    return 0;
}
