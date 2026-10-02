/* Non-linear editing: tracks, clips, per-clip effect stacks and keyframes.
 *
 * The effect stack reuses the node graph rather than reimplementing it: building
 * a frame means assembling a small graph of the clip's source node followed by
 * one node per enabled effect, then rendering that graph. So a grading operator
 * behaves identically whether it is reached through a node panel, a clip's layer
 * stack, or the timeline. See jfx_timeline.h for the model. */

#include "jfx/jfx_timeline.h"
#include "jfx/jfx_audio.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "jfx/jfx_image.h"
#include "tilly/allocator.h"

static void *alloc_bytes(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static void free_bytes(void *p) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), p);
}

static char *dup_string(const char *s) {
    if (!s) {
        return NULL;
    }
    const size_t n = strlen(s) + 1u;
    char *copy = alloc_bytes(n);
    if (copy) {
        memcpy(copy, s, n);
    }
    return copy;
}

static float clamp01(float v) {
    if (!(v > 0.0f)) return 0.0f;
    return v >= 1.0f ? 1.0f : v;
}

static float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

/* ---- Storage ------------------------------------------------------------- */

typedef struct {
    uint64_t frame;
    float value;
} timeline_key_t;

typedef struct {
    /* The kind's own parameter values, plus the text fields it declares. */
    float params[JFX_NODE_MAX_PARAMS];
    char *strings[JFX_GRAPH_MAX_STRING_PARAMS];
    bool enabled;
    float opacity;
    jfx_blend_mode_t blend;
    jfx_interp_t interp;
    const jfx_node_kind_t *kind;
    timeline_key_t *keys[JFX_NODE_MAX_PARAMS];
    size_t key_counts[JFX_NODE_MAX_PARAMS];
} effect_t;

typedef struct {
    char name[JFX_TIMELINE_NAME_MAX];
    char *image_path;
    jfx_clip_source_t source;
    float source_params[8];
    uint64_t start_frame;
    uint64_t length_frames;
    uint64_t in_point;
    int64_t key_offset;
    float opacity;
    jfx_blend_mode_t blend_mode;
    bool enabled;
    effect_t effects[JFX_TIMELINE_MAX_EFFECTS];
    jfx_clip_audio_t audio;
    size_t effect_count;
} clip_t;

typedef struct {
    char name[JFX_TIMELINE_NAME_MAX];
    bool muted;
    bool solo;
    float opacity;
    jfx_blend_mode_t blend;
    float audio_gain;
    clip_t clips[JFX_TIMELINE_MAX_CLIPS_PER_TRACK];
    size_t clip_count;
} track_t;

struct jfx_timeline {
    uint32_t width;
    uint32_t height;
    uint32_t fps_num;
    uint32_t fps_den;
    track_t tracks[JFX_TIMELINE_MAX_TRACKS];
    size_t track_count;
};

static void effect_release(effect_t *effect) {
    for (size_t i = 0; i < JFX_GRAPH_MAX_STRING_PARAMS; ++i) {
        free_bytes(effect->strings[i]);
        effect->strings[i] = NULL;
    }
    for (size_t p = 0; p < JFX_NODE_MAX_PARAMS; ++p) {
        free_bytes(effect->keys[p]);
        effect->keys[p] = NULL;
        effect->key_counts[p] = 0;
    }
}

static void clip_release(clip_t *clip) {
    free_bytes(clip->image_path);
    clip->image_path = NULL;
    for (size_t i = 0; i < clip->effect_count; ++i) {
        effect_release(&clip->effects[i]);
    }
    clip->effect_count = 0;
}

/* ---- Source names -------------------------------------------------------- */

static const char *const kSourceNames[JFX_CLIP_SOURCE_COUNT] = { "solid", "gradient", "checker",
    "sweep", "image", "video", "audio" };

const char *jfx_clip_source_name(jfx_clip_source_t source) {
    if (source < 0 || source >= JFX_CLIP_SOURCE_COUNT) {
        return "solid";
    }
    return kSourceNames[source];
}

jfx_clip_source_t jfx_clip_source_parse(const char *name) {
    if (!name) {
        return JFX_CLIP_SOURCE_COUNT;
    }
    for (int i = 0; i < JFX_CLIP_SOURCE_COUNT; ++i) {
        const char *a = kSourceNames[i];
        const char *b = name;
        while (*a && *b) {
            char cb = *b;
            if (cb >= 'A' && cb <= 'Z') {
                cb = (char)(cb - 'A' + 'a');
            }
            if (cb == '_' || cb == ' ') {
                cb = '-';
            }
            if (*a != cb) {
                break;
            }
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return (jfx_clip_source_t)i;
        }
    }
    return JFX_CLIP_SOURCE_COUNT;
}

/* ---- Construction -------------------------------------------------------- */

jfx_timeline_t *jfx_timeline_create(uint32_t width, uint32_t height, uint32_t fps_num,
    uint32_t fps_den) {
    if (!width || !height || !fps_num || !fps_den) {
        return NULL;
    }
    jfx_timeline_t *timeline = alloc_bytes(sizeof(*timeline));
    if (!timeline) {
        return NULL;
    }
    memset(timeline, 0, sizeof(*timeline));
    timeline->width = width;
    timeline->height = height;
    timeline->fps_num = fps_num;
    timeline->fps_den = fps_den;
    return timeline;
}

void jfx_timeline_destroy(jfx_timeline_t *timeline) {
    if (!timeline) {
        return;
    }
    for (size_t t = 0; t < timeline->track_count; ++t) {
        for (size_t c = 0; c < timeline->tracks[t].clip_count; ++c) {
            clip_release(&timeline->tracks[t].clips[c]);
        }
    }
    free_bytes(timeline);
}

uint32_t jfx_timeline_width(const jfx_timeline_t *timeline) {
    return timeline ? timeline->width : 0u;
}

uint32_t jfx_timeline_height(const jfx_timeline_t *timeline) {
    return timeline ? timeline->height : 0u;
}

double jfx_timeline_fps(const jfx_timeline_t *timeline) {
    if (!timeline || !timeline->fps_num || !timeline->fps_den) {
        return 0.0;
    }
    return (double)timeline->fps_num / (double)timeline->fps_den;
}

static int track_ok(const jfx_timeline_t *timeline, uint32_t track) {
    return timeline && track < timeline->track_count;
}

static clip_t *clip_at(jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    if (!track_ok(timeline, track) || clip >= timeline->tracks[track].clip_count) {
        return NULL;
    }
    return &timeline->tracks[track].clips[clip];
}

static const clip_t *clip_at_const(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    if (!track_ok(timeline, track) || clip >= timeline->tracks[track].clip_count) {
        return NULL;
    }
    return &timeline->tracks[track].clips[clip];
}

uint64_t jfx_timeline_duration(const jfx_timeline_t *timeline) {
    if (!timeline) {
        return 0u;
    }
    uint64_t longest = 0u;
    for (size_t t = 0; t < timeline->track_count; ++t) {
        for (size_t c = 0; c < timeline->tracks[t].clip_count; ++c) {
            const clip_t *clip = &timeline->tracks[t].clips[c];
            const uint64_t end = clip->start_frame + clip->length_frames;
            if (end > longest) {
                longest = end;
            }
        }
    }
    return longest;
}

jfx_result_t jfx_timeline_timecode(const jfx_timeline_t *timeline, uint64_t frame, char *out_text,
    size_t out_size) {
    if (!timeline || !out_text || !out_size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Timecode is always expressed against the nominal whole-number rate, so a
     * 30000/1001 sequence reads as 30 fps rather than truncating to 29. Rounding
     * rather than truncating matters here: 29.97 truncated is 29, which is not
     * the rate the frames actually play at. */
    const uint32_t den = timeline->fps_den ? timeline->fps_den : 1u;
    uint32_t rate = (uint32_t)(((uint64_t)timeline->fps_num + den / 2u) / den);
    if (rate == 0u) {
        rate = 1u;
    }
    const uint64_t total_seconds = frame / rate;
    const uint64_t frames = frame % rate;
    const uint64_t seconds = total_seconds % 60u;
    const uint64_t minutes = (total_seconds / 60u) % 60u;
    const uint64_t hours = total_seconds / 3600u;
    const int written = snprintf(out_text, out_size, "%02llu:%02llu:%02llu:%02llu",
        (unsigned long long)hours, (unsigned long long)minutes, (unsigned long long)seconds,
        (unsigned long long)frames);
    if (written < 0 || (size_t)written >= out_size) {
        return JFX_ERROR_BACKEND_FAILURE; /* the buffer was too small */
    }
    return JFX_SUCCESS;
}

/* ---- Tracks -------------------------------------------------------------- */

uint32_t jfx_timeline_add_track(jfx_timeline_t *timeline, const char *name) {
    if (!timeline) {
        return UINT32_MAX;
    }
    if (timeline->track_count >= JFX_TIMELINE_MAX_TRACKS) {
        return UINT32_MAX;
    }
    track_t *track = &timeline->tracks[timeline->track_count];
    memset(track, 0, sizeof(*track));
    track->opacity = 1.0f;
    track->audio_gain = 1.0f;
    track->blend = JFX_BLEND_NORMAL;
    snprintf(track->name, sizeof(track->name), "%s", name && name[0] ? name : "Track");
    timeline->track_count++;
    return (uint32_t)(timeline->track_count - 1u);
}

jfx_result_t jfx_timeline_remove_track(jfx_timeline_t *timeline, uint32_t track) {
    if (!track_ok(timeline, track)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    for (size_t c = 0; c < timeline->tracks[track].clip_count; ++c) {
        clip_release(&timeline->tracks[track].clips[c]);
    }
    /* Shift down so track indices stay in creation order, as with nodes. */
    const size_t last = timeline->track_count - 1u;
    if (track != last) {
        memmove(&timeline->tracks[track], &timeline->tracks[track + 1u],
            (last - (size_t)track) * sizeof(track_t));
    }
    memset(&timeline->tracks[last], 0, sizeof(track_t));
    timeline->track_count = last;
    return JFX_SUCCESS;
}

size_t jfx_timeline_track_count(const jfx_timeline_t *timeline) {
    return timeline ? timeline->track_count : 0u;
}

jfx_result_t jfx_timeline_move_track(jfx_timeline_t *timeline, uint32_t track, uint32_t to) {
    if (!track_ok(timeline,track) || !track_ok(timeline,to)) return JFX_ERROR_INVALID_ARGUMENT;
    if (track==to) return JFX_SUCCESS;
    track_t *moving=alloc_bytes(sizeof(*moving));
    if (!moving) return JFX_ERROR_OUT_OF_MEMORY;
    *moving=timeline->tracks[track];
    if (track<to) memmove(timeline->tracks+track,timeline->tracks+track+1,(to-track)*sizeof(*moving));
    else memmove(timeline->tracks+to+1,timeline->tracks+to,(track-to)*sizeof(*moving));
    timeline->tracks[to]=*moving; free_bytes(moving);
    return JFX_SUCCESS;
}

const char *jfx_timeline_track_name(const jfx_timeline_t *timeline, uint32_t track) {
    return track_ok(timeline, track) ? timeline->tracks[track].name : NULL;
}

jfx_result_t jfx_timeline_set_track_name(jfx_timeline_t *timeline, uint32_t track,
    const char *name) {
    if (!track_ok(timeline, track) || !name) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    snprintf(timeline->tracks[track].name, sizeof(timeline->tracks[track].name), "%s", name);
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_set_track_muted(jfx_timeline_t *timeline, uint32_t track, bool muted) {
    if (!track_ok(timeline, track)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    timeline->tracks[track].muted = muted;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_set_track_solo(jfx_timeline_t *timeline, uint32_t track, bool solo) {
    if (!track_ok(timeline, track)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    timeline->tracks[track].solo = solo;
    return JFX_SUCCESS;
}

bool jfx_timeline_track_muted(const jfx_timeline_t *timeline, uint32_t track) {
    return track_ok(timeline, track) && timeline->tracks[track].muted;
}

bool jfx_timeline_track_solo(const jfx_timeline_t *timeline, uint32_t track) {
    return track_ok(timeline, track) && timeline->tracks[track].solo;
}

jfx_result_t jfx_timeline_set_track_opacity(jfx_timeline_t *timeline, uint32_t track,
    float opacity) {
    if (!track_ok(timeline, track) || !isfinite(opacity)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    timeline->tracks[track].opacity = clamp01(opacity);
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_set_track_blend(jfx_timeline_t *timeline, uint32_t track,
    jfx_blend_mode_t mode) {
    if (!track_ok(timeline, track) || mode < 0 || mode >= JFX_BLEND_COUNT) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    timeline->tracks[track].blend = mode;
    return JFX_SUCCESS;
}

float jfx_timeline_track_opacity(const jfx_timeline_t *timeline, uint32_t track) {
    return track_ok(timeline, track) ? timeline->tracks[track].opacity : 0.0f;
}

jfx_blend_mode_t jfx_timeline_track_blend(const jfx_timeline_t *timeline, uint32_t track) {
    return track_ok(timeline, track) ? timeline->tracks[track].blend : JFX_BLEND_NORMAL;
}

/* ---- Clips --------------------------------------------------------------- */

uint32_t jfx_timeline_add_clip(jfx_timeline_t *timeline, uint32_t track,
    const jfx_clip_desc_t *desc) {
    if (!timeline || !desc || !track_ok(timeline, track)) {
        return UINT32_MAX;
    }
    if (desc->source < 0 || desc->source >= JFX_CLIP_SOURCE_COUNT || !desc->length_frames) {
        return UINT32_MAX;
    }
    if (desc->length_frames>INT64_MAX || desc->start_frame>(uint64_t)INT64_MAX-desc->length_frames ||
        desc->in_point>(uint64_t)INT64_MAX-desc->length_frames) return UINT32_MAX;
    for (size_t i=0;i<8;++i) if (!isfinite(desc->source_params[i])) return UINT32_MAX;
    if ((desc->source == JFX_CLIP_IMAGE || desc->source == JFX_CLIP_VIDEO || desc->source == JFX_CLIP_AUDIO) && (!desc->image_path || !desc->image_path[0])) {
        return UINT32_MAX;
    }
    if (desc->blend_mode < 0 || desc->blend_mode >= JFX_BLEND_COUNT) {
        return UINT32_MAX;
    }
    if (desc->image_path && strlen(desc->image_path)>=JFX_NODE_PATH_MAX) return UINT32_MAX;
    if (!isfinite(desc->opacity)) {
        return UINT32_MAX;
    }
    track_t *t = &timeline->tracks[track];
    if (t->clip_count >= JFX_TIMELINE_MAX_CLIPS_PER_TRACK) {
        return UINT32_MAX;
    }
    clip_t *clip = &t->clips[t->clip_count];
    memset(clip, 0, sizeof(*clip));
    snprintf(clip->name, sizeof(clip->name), "%s",
        desc->name && desc->name[0] ? desc->name : jfx_clip_source_name(desc->source));
    clip->source = desc->source;
    memcpy(clip->source_params, desc->source_params, sizeof(clip->source_params));
    if (desc->image_path && desc->image_path[0]) {
        clip->image_path = dup_string(desc->image_path);
        if (!clip->image_path) {
            memset(clip, 0, sizeof(*clip));
            return UINT32_MAX;
        }
    }
    clip->start_frame = desc->start_frame;
    clip->length_frames = desc->length_frames;
    clip->in_point = desc->in_point;
    clip->opacity = clamp01(desc->opacity);
    clip->blend_mode = desc->blend_mode;
    clip->enabled = desc->enabled;
    clip->audio=(jfx_clip_audio_t){sizeof(jfx_clip_audio_t),
        desc->source==JFX_CLIP_VIDEO || desc->source==JFX_CLIP_AUDIO,1,0,0,0,desc->length_frames};
    t->clip_count++;
    return (uint32_t)(t->clip_count - 1u);
}

jfx_result_t jfx_timeline_remove_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    clip_t *target = clip_at(timeline, track, clip);
    if (!target) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    clip_release(target);
    track_t *t = &timeline->tracks[track];
    const size_t last = t->clip_count - 1u;
    if (clip != last) {
        memmove(&t->clips[clip], &t->clips[clip + 1u], (last - (size_t)clip) * sizeof(clip_t));
    }
    memset(&t->clips[last], 0, sizeof(clip_t));
    t->clip_count = last;
    return JFX_SUCCESS;
}

size_t jfx_timeline_clip_count(const jfx_timeline_t *timeline, uint32_t track) {
    return track_ok(timeline, track) ? timeline->tracks[track].clip_count : 0u;
}

const char *jfx_timeline_clip_name(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->name : NULL;
}

jfx_result_t jfx_timeline_set_clip_name(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    const char *name) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || !name) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    snprintf(c->name, sizeof(c->name), "%s", name);
    return JFX_SUCCESS;
}

bool jfx_timeline_clip_covers(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint64_t frame) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c) {
        return false;
    }
    return frame >= c->start_frame && frame < c->start_frame + c->length_frames;
}

int jfx_timeline_clip_at(const jfx_timeline_t *timeline, uint32_t track, uint64_t frame) {
    if (!track_ok(timeline, track)) {
        return -1;
    }
    for (size_t i = 0; i < timeline->tracks[track].clip_count; ++i) {
        if (jfx_timeline_clip_covers(timeline, track, (uint32_t)i, frame)) {
            return (int)i;
        }
    }
    return -1;
}

jfx_result_t jfx_timeline_set_clip_enabled(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    bool enabled) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    c->enabled = enabled;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_set_clip_opacity(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    float opacity) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || !isfinite(opacity)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    c->opacity = clamp01(opacity);
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_set_clip_blend(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    jfx_blend_mode_t mode) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || mode < 0 || mode >= JFX_BLEND_COUNT) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    c->blend_mode = mode;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_move_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t to_index) {
    track_t *t = track_ok(timeline, track) ? &timeline->tracks[track] : NULL;
    if (!t || clip >= t->clip_count || to_index >= t->clip_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (clip == to_index) {
        return JFX_SUCCESS;
    }
    clip_t moving = t->clips[clip];
    if (clip < to_index) {
        /* Removing the clip closes the gap: everything after it shifts down, and
         * the moved clip lands at the far end of the run. */
        memmove(&t->clips[clip], &t->clips[clip + 1u],
            (to_index - (size_t)clip) * sizeof(clip_t));
    } else {
        /* Moving earlier: everything from the target up to the clip shifts up to
         * close the gap. Shifting the wrong range here would overwrite the moved
         * clip in place and drop it from the track entirely. */
        memmove(&t->clips[to_index + 1u], &t->clips[to_index],
            ((size_t)clip - to_index) * sizeof(clip_t));
    }
    t->clips[to_index] = moving;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_relocate_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t to_track, uint32_t to_index, bool keep_index) {
    clip_t *source = clip_at(timeline, track, clip);
    if (!source || !track_ok(timeline, to_track)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    track_t *from = &timeline->tracks[track];
    track_t *to = &timeline->tracks[to_track];
    if (keep_index) to_index=clip;
    if (to == from && (to_index >= from->clip_count || (keep_index && to_index == clip))) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (to != from && to->clip_count >= JFX_TIMELINE_MAX_CLIPS_PER_TRACK) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    if (to != from && to_index > to->clip_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Move the clip out without destroying it. Going through
     * jfx_timeline_remove_clip would free the path and the effect strings that
     * the copy about to be inserted still points at, so the extract has to shift
     * the array and clear the vacated slot itself. */
    clip_t moving = *source;
    memset(source, 0, sizeof(clip_t));
    if (clip != from->clip_count - 1u) {
        memmove(&from->clips[clip], &from->clips[clip + 1u],
            (from->clip_count - 1u - (size_t)clip) * sizeof(clip_t));
    }
    memset(&from->clips[from->clip_count - 1u], 0, sizeof(clip_t));
    from->clip_count--;
    if (to_index > to->clip_count) {
        return JFX_ERROR_INVALID_ARGUMENT; /* nothing to undo: the clip is gone */
    }

    if (to->clip_count >= JFX_TIMELINE_MAX_CLIPS_PER_TRACK) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    memmove(&to->clips[to_index + 1u], &to->clips[to_index],
        (to->clip_count - (size_t)to_index) * sizeof(clip_t));
    to->clips[to_index] = moving;
    to->clip_count++;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_trim_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    int64_t new_start, int64_t new_length) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || new_length < 1 || new_start<0 || new_start>INT64_MAX-new_length) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* Advancing the head advances the source and animation clocks by the same
     * amount, preserving the samples that remain visible. A tail-only trim
     * changes neither clock. */
    const int64_t old_start = (int64_t)c->start_frame;
    const int64_t delta = new_start - old_start;
    if ((delta<0 && c->in_point<(uint64_t)(-delta)) ||
        (delta>0 && c->in_point>(uint64_t)INT64_MAX-(uint64_t)delta)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const uint64_t new_in=delta<0?c->in_point-(uint64_t)(-delta):c->in_point+(uint64_t)delta;
    if (new_in>(uint64_t)INT64_MAX-(uint64_t)new_length) return JFX_ERROR_INVALID_ARGUMENT;
    if ((delta>0 && c->key_offset>INT64_MAX-delta) ||
        (delta<0 && c->key_offset<-INT64_MAX-delta)) return JFX_ERROR_INVALID_ARGUMENT;
    int64_t offset=c->key_offset+delta;
    if (offset>INT64_MAX-new_length) return JFX_ERROR_INVALID_ARGUMENT;
    c->start_frame = (uint64_t)new_start;
    c->length_frames = (uint64_t)new_length;
    c->in_point = new_in;
    c->key_offset=offset;
    return JFX_SUCCESS;
}

/* Copy owned strings and key arrays before publishing a new clip. */
static jfx_result_t clip_copy(const clip_t *src, clip_t *out) {
    *out=*src; out->image_path=NULL;
    for (size_t e=0;e<out->effect_count;++e) {
        memset(out->effects[e].strings,0,sizeof(out->effects[e].strings));
        memset(out->effects[e].keys,0,sizeof(out->effects[e].keys));
    }
    if (src->image_path && !(out->image_path=dup_string(src->image_path))) goto oom;
    for (size_t e=0;e<src->effect_count;++e) {
        for (size_t s=0;s<JFX_GRAPH_MAX_STRING_PARAMS;++s)
            if (src->effects[e].strings[s] && !(out->effects[e].strings[s]=dup_string(src->effects[e].strings[s]))) goto oom;
        for (size_t p=0;p<JFX_NODE_MAX_PARAMS;++p) {
            size_t bytes=src->effects[e].key_counts[p]*sizeof(timeline_key_t);
            if (!bytes) continue;
            out->effects[e].keys[p]=alloc_bytes(bytes);
            if (!out->effects[e].keys[p]) goto oom;
            memcpy(out->effects[e].keys[p],src->effects[e].keys[p],bytes);
        }
    }
    return JFX_SUCCESS;
oom:
    clip_release(out); return JFX_ERROR_OUT_OF_MEMORY;
}
jfx_result_t jfx_timeline_position_clip(jfx_timeline_t *t,uint32_t track,uint32_t clip,
    uint32_t to,uint64_t start) {
    clip_t *c=clip_at(t,track,clip);
    if (!c || !track_ok(t,to) || start>(uint64_t)INT64_MAX-c->length_frames) return JFX_ERROR_INVALID_ARGUMENT;
    if (track!=to && t->tracks[to].clip_count>=JFX_TIMELINE_MAX_CLIPS_PER_TRACK) return JFX_ERROR_OUT_OF_MEMORY;
    c->start_frame=start;
    if (track!=to) return jfx_timeline_relocate_clip(t,track,clip,to,(uint32_t)t->tracks[to].clip_count,false);
    return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_duplicate_clip(jfx_timeline_t *t,uint32_t track,uint32_t clip,
    uint32_t to,uint64_t start) {
    const clip_t *src=clip_at_const(t,track,clip);
    if (!src || !track_ok(t,to) || start>(uint64_t)INT64_MAX-src->length_frames) return JFX_ERROR_INVALID_ARGUMENT;
    if (t->tracks[to].clip_count>=JFX_TIMELINE_MAX_CLIPS_PER_TRACK) return JFX_ERROR_OUT_OF_MEMORY;
    clip_t copy; jfx_result_t r=clip_copy(src,&copy);
    if (r!=JFX_SUCCESS) return r;
    copy.start_frame=start;
    t->tracks[to].clips[t->tracks[to].clip_count++]=copy;
    return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_split_clip(jfx_timeline_t *t,uint32_t track,uint32_t clip,uint64_t frame) {
    clip_t *src=clip_at(t,track,clip);
    if (!src || frame<=src->start_frame || frame>=src->start_frame+src->length_frames) return JFX_ERROR_INVALID_ARGUMENT;
    track_t *lane=&t->tracks[track];
    if (lane->clip_count>=JFX_TIMELINE_MAX_CLIPS_PER_TRACK) return JFX_ERROR_OUT_OF_MEMORY;
    clip_t right; jfx_result_t r=clip_copy(src,&right);
    if (r!=JFX_SUCCESS) return r;
    uint64_t left=frame-src->start_frame;
    right.start_frame=frame; right.in_point+=left; right.length_frames-=left;
    right.key_offset+=(int64_t)left;
    src->length_frames=left;
    memmove(lane->clips+clip+2,lane->clips+clip+1,(lane->clip_count-clip-1)*sizeof(clip_t));
    lane->clips[clip+1]=right; lane->clip_count++;
    return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_slip_clip(jfx_timeline_t *t,uint32_t track,uint32_t clip,int64_t delta) {
    clip_t *c=clip_at(t,track,clip);
    if (!c || delta==INT64_MIN || (delta<0 && c->in_point<(uint64_t)(-delta)) ||
        (delta>0 && (uint64_t)delta>(uint64_t)INT64_MAX-c->in_point-c->length_frames)) return JFX_ERROR_INVALID_ARGUMENT;
    c->in_point=delta<0?c->in_point-(uint64_t)(-delta):c->in_point+(uint64_t)delta;
    return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_ripple_delete(jfx_timeline_t *t,uint32_t track,uint32_t clip) {
    const clip_t *c=clip_at_const(t,track,clip);
    if (!c) return JFX_ERROR_INVALID_ARGUMENT;
    uint64_t start=c->start_frame, length=c->length_frames, end=start+length;
    track_t *lane=&t->tracks[track];
    for (size_t i=0;i<lane->clip_count;++i) {
        const clip_t *other=lane->clips+i;
        if (i==clip) continue;
        if (other->start_frame<end && other->start_frame+other->length_frames>start) return JFX_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i=0;i<lane->clip_count;++i) if (lane->clips[i].start_frame>=end) {
        lane->clips[i].start_frame-=length;
    }
    return jfx_timeline_remove_clip(t,track,clip);
}
jfx_result_t jfx_timeline_ripple_insert(jfx_timeline_t *t,uint32_t track,uint64_t frame,uint64_t length) {
    if (!track_ok(t,track) || !length || length>INT64_MAX || frame>(uint64_t)INT64_MAX-length) return JFX_ERROR_INVALID_ARGUMENT;
    track_t *lane=&t->tracks[track];
    for (size_t i=0;i<lane->clip_count;++i) {
        const clip_t *c=lane->clips+i;
        if (c->start_frame<frame && c->start_frame+c->length_frames>frame) return JFX_ERROR_INVALID_ARGUMENT;
        if (c->start_frame>=frame && c->start_frame+c->length_frames>(uint64_t)INT64_MAX-length) return JFX_ERROR_INVALID_ARGUMENT;
    }
    for (size_t i=0;i<lane->clip_count;++i) if (lane->clips[i].start_frame>=frame) {
        lane->clips[i].start_frame+=length;
    }
    return JFX_SUCCESS;
}

jfx_clip_source_t jfx_timeline_clip_source(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->source : JFX_CLIP_SOLID;
}

const char *jfx_timeline_clip_path(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->image_path : NULL;
}

const float *jfx_timeline_clip_params(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip) {
    static const float kNone[8] = { 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->source_params : kNone;
}

bool jfx_timeline_clip_enabled(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->enabled : false;
}

uint32_t jfx_timeline_fps_num(const jfx_timeline_t *timeline) {
    return timeline ? timeline->fps_num : 0u;
}

uint32_t jfx_timeline_fps_den(const jfx_timeline_t *timeline) {
    return timeline ? timeline->fps_den : 1u;
}

uint64_t jfx_timeline_clip_start(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->start_frame : 0u;
}

uint64_t jfx_timeline_clip_length(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->length_frames : 0u;
}

uint64_t jfx_timeline_clip_in_point(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->in_point : 0u;
}
int64_t jfx_timeline_clip_key_offset(const jfx_timeline_t *t,uint32_t track,uint32_t clip) {
    const clip_t *c=clip_at_const(t,track,clip); return c?c->key_offset:0;
}
jfx_result_t jfx_timeline_set_clip_key_offset(jfx_timeline_t *t,uint32_t track,uint32_t clip,int64_t offset) {
    clip_t *c=clip_at(t,track,clip);
    if (!c || offset==INT64_MIN || offset>INT64_MAX-(int64_t)c->length_frames) return JFX_ERROR_INVALID_ARGUMENT;
    c->key_offset=offset; return JFX_SUCCESS;
}
static uint64_t clip_key_frame(const clip_t *c,uint64_t frame) {
    uint64_t local=frame>=c->start_frame?frame-c->start_frame:0;
    if (c->key_offset<0) return local>(uint64_t)(-c->key_offset)?local-(uint64_t)(-c->key_offset):0;
    return local>UINT64_MAX-(uint64_t)c->key_offset?UINT64_MAX:local+(uint64_t)c->key_offset;
}

float jfx_timeline_clip_opacity(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->opacity : 0.0f;
}

jfx_blend_mode_t jfx_timeline_clip_blend(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->blend_mode : JFX_BLEND_NORMAL;
}

/* ---- Effects ------------------------------------------------------------- */

static effect_t *effect_at(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return NULL;
    }
    return &c->effects[effect];
}

size_t jfx_timeline_effect_count(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    return c ? c->effect_count : 0u;
}

uint32_t jfx_timeline_add_effect(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    const char *kind_name) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || !kind_name) {
        return UINT32_MAX;
    }
    const jfx_node_kind_t *kind = jfx_node_kind_find(kind_name);
    if (!kind) {
        return UINT32_MAX;
    }
    /* A stack stage transforms one image. A source has no image input, and a
     * blend needs two, so neither can be a stage. */
    if (kind->input_count != 1u || kind->inputs[0].type != JFX_PORT_IMAGE) {
        return UINT32_MAX;
    }
    if (c->effect_count >= JFX_TIMELINE_MAX_EFFECTS) {
        return UINT32_MAX;
    }
    effect_t *effect = &c->effects[c->effect_count];
    memset(effect, 0, sizeof(*effect));
    effect->kind = kind;
    effect->enabled = true;
    effect->opacity = 1.0f;
    effect->blend = JFX_BLEND_NORMAL;
    effect->interp = JFX_INTERP_LINEAR;
    for (size_t p = 0; p < kind->param_count && p < JFX_NODE_MAX_PARAMS; ++p) {
        effect->params[p] = kind->params[p].default_value;
    }
    c->effect_count++;
    return (uint32_t)(c->effect_count - 1u);
}

jfx_result_t jfx_timeline_remove_effect(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    effect_release(&c->effects[effect]);
    const size_t last = c->effect_count - 1u;
    if (effect != last) {
        memmove(&c->effects[effect], &c->effects[effect + 1u],
            (last - (size_t)effect) * sizeof(effect_t));
    }
    memset(&c->effects[last], 0, sizeof(effect_t));
    c->effect_count = last;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_move_effect(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, uint32_t to_index) {
    clip_t *c = clip_at(timeline, track, clip);
    if (!c || effect >= c->effect_count || to_index >= c->effect_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (effect == to_index) {
        return JFX_SUCCESS;
    }
    effect_t moving = c->effects[effect];
    if (effect < to_index) {
        memmove(&c->effects[effect], &c->effects[effect + 1u],
            (to_index - (size_t)effect) * sizeof(effect_t));
    } else {
        memmove(&c->effects[effect + 1u], &c->effects[effect],
            ((size_t)effect - to_index) * sizeof(effect_t));
    }
    c->effects[to_index] = moving;
    return JFX_SUCCESS;
}

const char *jfx_timeline_effect_kind(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return NULL;
    }
    return c->effects[effect].kind->name;
}

const jfx_node_kind_t *jfx_timeline_effect_kind_desc(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip, uint32_t effect) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return NULL;
    }
    return c->effects[effect].kind;
}

jfx_result_t jfx_timeline_set_effect_enabled(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, bool enabled) {
    effect_t *e = effect_at(timeline, track, clip, effect);
    if (!e) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    e->enabled = enabled;
    return JFX_SUCCESS;
}

bool jfx_timeline_effect_enabled(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return false;
    }
    return c->effects[effect].enabled;
}

jfx_result_t jfx_timeline_set_effect_opacity(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, float opacity) {
    effect_t *e = effect_at(timeline, track, clip, effect);
    if (!e || !isfinite(opacity)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    e->opacity = clamp01(opacity);
    return JFX_SUCCESS;
}

float jfx_timeline_effect_opacity(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return 0.0f;
    }
    return c->effects[effect].opacity;
}

jfx_result_t jfx_timeline_set_effect_blend(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, jfx_blend_mode_t mode) {
    effect_t *e = effect_at(timeline, track, clip, effect);
    if (!e || mode < 0 || mode >= JFX_BLEND_COUNT) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    e->blend = mode;
    return JFX_SUCCESS;
}

jfx_blend_mode_t jfx_timeline_effect_blend(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip, uint32_t effect) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return JFX_BLEND_NORMAL;
    }
    return c->effects[effect].blend;
}

jfx_result_t jfx_timeline_set_effect_param(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, float value) {
    effect_t *e = effect_at(timeline, track, clip, effect);
    if (!e || param >= e->kind->param_count || param >= JFX_NODE_MAX_PARAMS || !isfinite(value)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    e->params[param] = value;
    return JFX_SUCCESS;
}

float jfx_timeline_effect_param(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count || param >= JFX_NODE_MAX_PARAMS) {
        return 0.0f;
    }
    return c->effects[effect].params[param];
}

jfx_result_t jfx_timeline_set_effect_string(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t index, const char *text) {
    effect_t *e = effect_at(timeline, track, clip, effect);
    if (!e || index >= e->kind->string_count || index >= JFX_GRAPH_MAX_STRING_PARAMS ||
        (text && strlen(text)>=JFX_NODE_PATH_MAX)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* A NULL text clears the slot, which is how a panel says "no file chosen". */
    char *copy = text ? dup_string(text) : NULL;
    if (text && !copy) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    free_bytes(e->strings[index]);
    e->strings[index] = copy;
    return JFX_SUCCESS;
}

const char *jfx_timeline_effect_string(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t index) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count || index >= JFX_GRAPH_MAX_STRING_PARAMS) {
        return NULL;
    }
    return c->effects[effect].strings[index];
}

/* ---- Keyframes ----------------------------------------------------------- */

jfx_result_t jfx_timeline_set_effect_interp(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, jfx_interp_t interp) {
    effect_t *e = effect_at(timeline, track, clip, effect);
    if (!e || interp < 0 || interp > JFX_INTERP_SMOOTH) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    e->interp = interp;
    return JFX_SUCCESS;
}

jfx_interp_t jfx_timeline_effect_interp(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count) {
        return JFX_INTERP_LINEAR;
    }
    return c->effects[effect].interp;
}

size_t jfx_timeline_key_count(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect >= c->effect_count || param >= JFX_NODE_MAX_PARAMS) {
        return 0u;
    }
    return c->effects[effect].key_counts[param];
}

/* Keys are held sorted by frame with a binary search, because evaluation looks
 * up the bracketing pair on every rendered frame. */
static timeline_key_t *key_find(const effect_t *effect, size_t param, uint64_t frame) {
    size_t low = 0, high = effect->key_counts[param];
    while (low < high) {
        const size_t mid = low + (high - low) / 2u;
        if (effect->keys[param][mid].frame == frame) {
            return &effect->keys[param][mid];
        }
        if (effect->keys[param][mid].frame < frame) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return NULL;
}

static size_t key_lower_bound(const effect_t *effect, size_t param, uint64_t frame) {
    size_t low = 0, high = effect->key_counts[param];
    while (low < high) {
        const size_t mid = low + (high - low) / 2u;
        if (effect->keys[param][mid].frame < frame) {
            low = mid + 1u;
        } else {
            high = mid;
        }
    }
    return low;
}

jfx_result_t jfx_timeline_add_key(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param, uint64_t frame, float value) {
    effect_t *e = effect_at(timeline, track, clip, effect_index);
    if (!e || param >= e->kind->param_count || param >= JFX_NODE_MAX_PARAMS || !isfinite(value) || frame>INT64_MAX) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (key_find(e, param, frame)) {
        /* A frame holds one key; adding again edits it. */
        key_find(e, param, frame)->value = value;
        return JFX_SUCCESS;
    }
    if (e->key_counts[param] >= JFX_TIMELINE_MAX_KEYS) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    const size_t at = key_lower_bound(e, param, frame);
    const size_t grown = e->key_counts[param] + 1u;
    timeline_key_t *keys = alloc_bytes(grown * sizeof(timeline_key_t));
    if (!keys) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    if (at) memcpy(keys, e->keys[param], at * sizeof(timeline_key_t));
    keys[at].frame = frame;
    keys[at].value = value;
    if (e->key_counts[param]>at) memcpy(keys + at + 1u, e->keys[param] + at,
        (e->key_counts[param] - at) * sizeof(timeline_key_t));
    free_bytes(e->keys[param]);
    e->keys[param] = keys;
    e->key_counts[param] = grown;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_remove_key(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param, uint64_t frame) {
    effect_t *e = effect_at(timeline, track, clip, effect_index);
    if (!e || param >= JFX_NODE_MAX_PARAMS) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t at = key_lower_bound(e, param, frame);
    if (at >= e->key_counts[param] || e->keys[param][at].frame != frame) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memmove(&e->keys[param][at], &e->keys[param][at + 1u],
        (e->key_counts[param] - at - 1u) * sizeof(timeline_key_t));
    e->key_counts[param]--;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_clear_keys(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param) {
    effect_t *e = effect_at(timeline, track, clip, effect_index);
    if (!e || param >= JFX_NODE_MAX_PARAMS) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    free_bytes(e->keys[param]);
    e->keys[param] = NULL;
    e->key_counts[param] = 0;
    return JFX_SUCCESS;
}

bool jfx_timeline_key_at(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param, uint64_t frame, float *out_value) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect_index >= c->effect_count || param >= JFX_NODE_MAX_PARAMS) {
        return false;
    }
    const timeline_key_t *key = key_find(&c->effects[effect_index], param, frame);
    if (!key) {
        return false;
    }
    if (out_value) {
        *out_value = key->value;
    }
    return true;
}

/* The interpolated value of a keyframed parameter at a frame. Holds the value of
 * the first key before the first frame and of the last key after it, so a
 * parameter never extrapolates. Shared by the public getter and the renderer so
 * the two cannot disagree. */
static float evaluate_param(const effect_t *effect, size_t param, uint64_t frame) {
    const size_t count = effect->key_counts[param];
    if (count == 0u) {
        return effect->params[param];
    }
    const timeline_key_t *keys = effect->keys[param];
    if (frame <= keys[0].frame) {
        return keys[0].value;
    }
    if (frame >= keys[count - 1u].frame) {
        return keys[count - 1u].value;
    }
    const size_t at = key_lower_bound(effect, param, frame);
    if (at < count && keys[at].frame == frame) {
        return keys[at].value;
    }
    const timeline_key_t *lo = &keys[at - 1u];
    const timeline_key_t *hi = &keys[at];
    if (effect->interp == JFX_INTERP_HOLD) {
        return lo->value;
    }
    const uint64_t span = hi->frame - lo->frame;
    const float t = span ? (float)(frame - lo->frame) / (float)span : 0.0f;
    if (effect->interp == JFX_INTERP_SMOOTH) {
        return lerp(lo->value, hi->value, t * t * (3.0f - 2.0f * t));
    }
    return lerp(lo->value, hi->value, t);
}

bool jfx_timeline_key_by_index(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param, size_t index, uint64_t *out_frame, float *out_value) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect_index >= c->effect_count || param >= JFX_NODE_MAX_PARAMS) {
        return false;
    }
    const effect_t *e = &c->effects[effect_index];
    if (index >= e->key_counts[param]) {
        return false;
    }
    if (out_frame) {
        *out_frame = e->keys[param][index].frame;
    }
    if (out_value) {
        *out_value = e->keys[param][index].value;
    }
    return true;
}

bool jfx_timeline_first_key(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param, uint64_t *out_frame) {
    float value = 0.0f;
    return jfx_timeline_key_by_index(timeline, track, clip, effect_index, param, 0, out_frame,
        &value);
}

float jfx_timeline_effect_param_at(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect_index, size_t param, uint64_t frame) {
    const clip_t *c = clip_at_const(timeline, track, clip);
    if (!c || effect_index >= c->effect_count || param >= JFX_NODE_MAX_PARAMS) {
        return 0.0f;
    }
    return evaluate_param(&c->effects[effect_index], param, frame);
}
float jfx_timeline_effect_param_on_timeline(const jfx_timeline_t *t,uint32_t track,uint32_t clip,
    uint32_t effect,size_t param,uint64_t frame) {
    const clip_t *c=clip_at_const(t,track,clip);
    if (!c || effect>=c->effect_count || param>=JFX_NODE_MAX_PARAMS) return 0;
    return evaluate_param(c->effects+effect,param,clip_key_frame(c,frame));
}

/* ---- Rendering ----------------------------------------------------------- */

/* Defined with the keyframe accessors above; declared here because the renderer
 * reads keyframed parameters while building a clip's graph. */
static float evaluate_param(const effect_t *effect, size_t param, uint64_t frame);

/* Builds a graph for one clip: its source node, then one node per enabled
 * effect, then a blend per effect whose mode or opacity differs from the
 * default. Reusing the node graph keeps one implementation of every operator. */
static jfx_result_t build_clip_graph(const clip_t *clip, uint64_t frame, jfx_graph_t **out_graph,
    uint32_t *out_output) {
    jfx_graph_t *graph = jfx_graph_create();
    if (!graph) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    uint32_t source = 0;
    jfx_result_t status;
    switch (clip->source) {
    case JFX_CLIP_GRADIENT: status = jfx_graph_add_node(graph, "linear_gradient", NULL, &source); break;
    case JFX_CLIP_CHECKER: status = jfx_graph_add_node(graph, "checker", NULL, &source); break;
    case JFX_CLIP_SWEEP: status = jfx_graph_add_node(graph, "sweep", NULL, &source); break;
    case JFX_CLIP_VIDEO: status = jfx_graph_add_node(graph, "video", NULL, &source); break;
    case JFX_CLIP_IMAGE: status = jfx_graph_add_node(graph, "image", NULL, &source); break;
    case JFX_CLIP_SOLID:
    default: status = jfx_graph_add_node(graph, "solid", NULL, &source); break;
    }
    if (status != JFX_SUCCESS) {
        jfx_graph_destroy(graph);
        return status;
    }

    if (clip->source == JFX_CLIP_IMAGE || clip->source == JFX_CLIP_VIDEO) {
        if (!clip->image_path) {
            jfx_graph_destroy(graph);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        /* The graph owns its copy, so the clip's path only has to outlive this
         * call. */
        status = jfx_graph_set_node_string(graph, source, 0, clip->image_path);
        if (status != JFX_SUCCESS) {
            jfx_graph_destroy(graph);
            return status;
        }
    } else if (clip->source == JFX_CLIP_SOLID) {
        /* A solid takes a colour, so a colour node feeds it. */
        uint32_t color = 0;
        status = jfx_graph_add_node(graph, "color", NULL, &color);
        if (status != JFX_SUCCESS) {
            jfx_graph_destroy(graph);
            return status;
        }
        jfx_node_value_t *cv = jfx_graph_node_value_mut(graph, color);
        for (int i = 0; i < 4; ++i) {
            cv->scalars[i] = clip->source_params[i];
        }
        if (jfx_graph_connect(graph, color, 0, source, 0) != JFX_SUCCESS) {
            jfx_graph_destroy(graph);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
    } else if (clip->source == JFX_CLIP_SWEEP) {
        /* A test pattern takes no inputs: its one parameter is the pattern. */
        jfx_graph_node_value_mut(graph, source)->scalars[0] = clip->source_params[0];
    } else {
        /* The gradient and the checker each take two colours, held in slots 0-2
         * and 3-5, and one number in slot 6: an angle for the gradient, a tile
         * size for the checker. */
        for (int side = 0; side < 2; ++side) {
            uint32_t color = 0;
            if (jfx_graph_add_node(graph, "color", NULL, &color) != JFX_SUCCESS) {
                jfx_graph_destroy(graph);
                return JFX_ERROR_OUT_OF_MEMORY;
            }
            jfx_node_value_t *cv = jfx_graph_node_value_mut(graph, color);
            for (int i = 0; i < 3; ++i) {
                cv->scalars[i] = clip->source_params[side * 3 + i];
            }
            cv->scalars[3] = 1.0f;
            if (jfx_graph_connect(graph, color, 0, source, (size_t)side) != JFX_SUCCESS) {
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        }
        jfx_graph_node_value_mut(graph, source)->scalars[0] = clip->source_params[6];
    }

    uint32_t head = source;
    for (size_t i = 0; i < clip->effect_count; ++i) {
        const effect_t *effect = &clip->effects[i];
        if (!effect->enabled) {
            continue;
        }
        uint32_t node = 0;
        if (jfx_graph_add_node(graph, effect->kind->name, NULL, &node) != JFX_SUCCESS) {
            jfx_graph_destroy(graph);
            return JFX_ERROR_OUT_OF_MEMORY;
        }
        /* A keyframed parameter is evaluated at this frame, in the clip's own
         * time base: a clip's frame 0 is the start of the clip, so a wipe
         * written against the clip behaves the same wherever it sits. */
        const uint64_t local = clip_key_frame(clip,frame);
        for (size_t p = 0; p < effect->kind->param_count && p < JFX_NODE_MAX_PARAMS; ++p) {
            const size_t keys = effect->key_counts[p];
            const float value = keys ? evaluate_param(effect, p, local) : effect->params[p];
            jfx_graph_node_value_mut(graph, node)->scalars[p] = value;
        }
        for (size_t s = 0; s < effect->kind->string_count && s < JFX_GRAPH_MAX_STRING_PARAMS; ++s) {
            const char *text = effect->strings[s];
            if (text && text[0]) {
                status = jfx_graph_set_node_string(graph, node, s, text);
                if (status != JFX_SUCCESS) {
                    jfx_graph_destroy(graph);
                    return status;
                }
            }
        }
        if (jfx_graph_connect(graph, head, 0, node, 0) != JFX_SUCCESS) {
            jfx_graph_destroy(graph);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        head = node;
    }
    *out_graph = graph;
    *out_output = head;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_render(const jfx_timeline_t *timeline, uint64_t frame, float time_seconds,
    uint8_t *out_pixels) {
    if (!timeline || !out_pixels) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const uint32_t width = timeline->width;
    const uint32_t height = timeline->height;
    const size_t pixel_count = (size_t)width * (size_t)height;
    if (!pixel_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    /* A muted or solo filter decides which tracks take part. A solo anywhere
     * means the others are skipped, which is how a UI isolates one layer. */
    bool any_solo = false;
    for (size_t t = 0; t < timeline->track_count; ++t) {
        if (timeline->tracks[t].solo) {
            any_solo = true;
            break;
        }
    }

    /* Start from a transparent frame: an empty timeline renders as nothing
     * rather than black, so a layer can fade up from clear. */
    memset(out_pixels, 0, pixel_count * 4u);

    /* Tracks composite from the bottom up, so a higher index sits above a lower
     * one. Each track's layer is blended over the accumulated result rather than
     * copied, because the layer starts transparent and the track below must show
     * through a partly opaque clip. */
    for (size_t t = 0; t < timeline->track_count; ++t) {
        const track_t *track = &timeline->tracks[t];
        if (track->muted || (any_solo && !track->solo)) {
            continue;
        }
        uint8_t *layer = NULL;
        for (size_t c = 0; c < track->clip_count; ++c) {
            const clip_t *clip = &track->clips[c];
            if (clip->source==JFX_CLIP_AUDIO || !clip->enabled || !jfx_timeline_clip_covers(timeline, (uint32_t)t, (uint32_t)c,
                    frame)) {
                continue;
            }
            jfx_graph_t *graph = NULL;
            uint32_t output = 0;
            const jfx_result_t built=build_clip_graph(clip,frame,&graph,&output);
            if (built!=JFX_SUCCESS) {
                free_bytes(layer); return built;
            }
            uint8_t *clip_pixels = alloc_bytes(pixel_count * 4u);
            if (!clip_pixels) {
                jfx_graph_destroy(graph);
                free_bytes(layer);
                return JFX_ERROR_OUT_OF_MEMORY;
            }
            const jfx_result_t status =
                jfx_graph_render(graph, output, width, height,
                    clip->source == JFX_CLIP_VIDEO ? (float)((double)(frame - clip->start_frame + clip->in_point) / jfx_timeline_fps(timeline)) : time_seconds, clip_pixels);
            jfx_graph_destroy(graph);
            if (status != JFX_SUCCESS) {
                free_bytes(clip_pixels);
                free_bytes(layer);
                return status;
            }
            if (!layer) {
                /* The first live clip must obey its opacity too. Preserve straight
                 * RGB while scaling alpha over the track's transparent backdrop. */
                for (size_t p=0;p<pixel_count;++p) clip_pixels[p*4+3]=(uint8_t)lrintf((float)clip_pixels[p*4+3]*clip->opacity);
                layer = clip_pixels;
                continue;
            }
            /* Clip N sits over clip N-1 with the clip's own blend mode; the
             * later clip's opacity scales its contribution. */
            jfx_blend_rgba8(layer, clip_pixels, pixel_count, clip->blend_mode, clip->opacity);
            free_bytes(clip_pixels);
        }
        if (!layer) {
            continue;
        }
        jfx_blend_rgba8(out_pixels, layer, pixel_count, track->blend, track->opacity);
        free_bytes(layer);
    }
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_get_clip_audio(const jfx_timeline_t *t,uint32_t track,uint32_t clip,jfx_clip_audio_t *out) {
    const clip_t *c=clip_at_const(t,track,clip);
    if (!c || !out || out->size<sizeof(*out)) return JFX_ERROR_INVALID_ARGUMENT;
    *out=c->audio; return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_set_clip_audio(jfx_timeline_t *t,uint32_t track,uint32_t clip,const jfx_clip_audio_t *audio) {
    clip_t *c=clip_at(t,track,clip);
    if (!c || !audio || audio->size<sizeof(*audio) || !isfinite(audio->gain) || audio->gain<0 || audio->gain>16 ||
        !isfinite(audio->pan) || audio->pan<-1 || audio->pan>1 || !audio->reference_frames || audio->reference_frames>INT64_MAX ||
        audio->fade_in_frames>INT64_MAX || audio->fade_out_frames>INT64_MAX ||
        (audio->enabled && c->source!=JFX_CLIP_AUDIO && c->source!=JFX_CLIP_VIDEO)) return JFX_ERROR_INVALID_ARGUMENT;
    c->audio=*audio; c->audio.size=sizeof(c->audio); return JFX_SUCCESS;
}
jfx_result_t jfx_timeline_set_track_audio_gain(jfx_timeline_t *t,uint32_t track,float gain) {
    if (!track_ok(t,track) || !isfinite(gain) || gain<0 || gain>16) return JFX_ERROR_INVALID_ARGUMENT;
    t->tracks[track].audio_gain=gain; return JFX_SUCCESS;
}
float jfx_timeline_track_audio_gain(const jfx_timeline_t *t,uint32_t track) {
    return track_ok(t,track)?t->tracks[track].audio_gain:0;
}

jfx_result_t jfx_timeline_render_image(const jfx_timeline_t *timeline, uint64_t frame,
    float time_seconds, jfx_image_t *out_image) {
    if (!out_image || out_image->size < sizeof(*out_image)) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    memset(out_image, 0, sizeof(*out_image));
    out_image->size = sizeof(*out_image);
    if (!timeline) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const size_t bytes = (size_t)timeline->width * (size_t)timeline->height * 4u;
    uint8_t *pixels = alloc_bytes(bytes);
    if (!pixels) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    const jfx_result_t status = jfx_timeline_render(timeline, frame, time_seconds, pixels);
    if (status != JFX_SUCCESS) {
        free_bytes(pixels);
        return status;
    }
    out_image->width = timeline->width;
    out_image->height = timeline->height;
    out_image->channels = 4u;
    out_image->pixels = pixels;
    return JFX_SUCCESS;
}

jfx_result_t jfx_timeline_describe_track(const jfx_timeline_t *timeline, uint32_t track, char *out_text,
    size_t out_size) {
    if (!track_ok(timeline, track) || !out_text || !out_size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const track_t *t = &timeline->tracks[track];
    uint64_t end = 0u;
    for (size_t c = 0; c < t->clip_count; ++c) {
        const uint64_t clip_end = t->clips[c].start_frame + t->clips[c].length_frames;
        if (clip_end > end) {
            end = clip_end;
        }
    }
    char timecode[32] = "00:00:00:00";
    if (jfx_timeline_timecode(timeline, end, timecode, sizeof(timecode)) != JFX_SUCCESS) {
        return JFX_ERROR_BACKEND_FAILURE;
    }
    const int written = snprintf(out_text, out_size, "%zu clip%s, %s", t->clip_count,
        t->clip_count == 1u ? "" : "s", timecode);
    if (written < 0 || (size_t)written >= out_size) {
        return JFX_ERROR_BACKEND_FAILURE;
    }
    return JFX_SUCCESS;
}
