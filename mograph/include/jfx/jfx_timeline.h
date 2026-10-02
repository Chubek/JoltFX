#ifndef JFX_TIMELINE_H
#define JFX_TIMELINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jfx_compose.h"
#include "jfx_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Non-linear editing.
 *
 * A timeline holds tracks; a track holds clips; a clip holds an ordered stack
 * of effects. Rendering a frame walks the tracks from the bottom up, resolves the
 * clips that are live at that frame, renders each clip's source, runs its effect
 * stack, and composites the result onto the accumulated frame. That is the whole
 * model, and it is the same model the layer-based effects panel edits: a clip's
 * effect stack is a list of named, parameterised, re-orderable, individually
 * enabled and individually blendable stages.
 *
 * Time is counted in frames against an exact rational frame rate, so a 30000/1001
 * sequence is not rounded to 29.97 and drift never accumulates. A clip's
 * `in_point` is the frame of its source that lands on the clip's start frame,
 * which makes trim and source-slip edits expressible.
 *
 * Clip sources are the graph's source nodes, so a clip can be a flat colour, a
 * gradient, a still image, video (with FFmpeg) or a test pattern. Its effect stack
 * reuses the node evaluator rather than a second implementation. */

#define JFX_TIMELINE_MAX_TRACKS 16
#define JFX_TIMELINE_MAX_CLIPS_PER_TRACK 128
#define JFX_TIMELINE_MAX_EFFECTS 32
#define JFX_TIMELINE_MAX_KEYS 64
#define JFX_TIMELINE_NAME_MAX 64
#define JFX_TIMELINE_API_MAJOR 1
#define JFX_TIMELINE_API_MINOR 2

/* How a value between two keyframes is chosen. */
typedef enum {
    JFX_INTERP_LINEAR = 0,
    JFX_INTERP_HOLD,     /* step: the left key's value until the right one */
    JFX_INTERP_SMOOTH    /* smoothstep between the two keys */
} jfx_interp_t;

/* What a clip plays. */
typedef enum {
    JFX_CLIP_SOLID = 0,   /* a flat colour */
    JFX_CLIP_GRADIENT,
    JFX_CLIP_CHECKER,
    JFX_CLIP_SWEEP,       /* a test pattern */
    JFX_CLIP_IMAGE,       /* a still image on disk */
    JFX_CLIP_VIDEO,       /* local video file; requires FFmpeg */
    JFX_CLIP_AUDIO,       /* local audio; no visual contribution */
    JFX_CLIP_SOURCE_COUNT
} jfx_clip_source_t;

const char *jfx_clip_source_name(jfx_clip_source_t source);
jfx_clip_source_t jfx_clip_source_parse(const char *name);

typedef struct jfx_timeline jfx_timeline_t;

/* ---- Construction -------------------------------------------------------- */

/* `width` and `height` are the sequence's raster; `fps_num`/`fps_den` the exact
 * frame rate. A rate of 0/1 is rejected. */
jfx_timeline_t *jfx_timeline_create(uint32_t width, uint32_t height, uint32_t fps_num,
    uint32_t fps_den);
void jfx_timeline_destroy(jfx_timeline_t *timeline);

uint32_t jfx_timeline_width(const jfx_timeline_t *timeline);
uint32_t jfx_timeline_height(const jfx_timeline_t *timeline);
/* Frames per second as a double, for a UI that shows a timecode. */
double jfx_timeline_fps(const jfx_timeline_t *timeline);

/* The longest frame covered by any clip, which is the sequence's duration. */
uint64_t jfx_timeline_duration(const jfx_timeline_t *timeline);
/* A timecode string for a frame, e.g. "00:00:01:12". */
jfx_result_t jfx_timeline_timecode(const jfx_timeline_t *timeline, uint64_t frame, char *out_text,
    size_t out_size);

/* ---- Tracks -------------------------------------------------------------- */

/* Track indices are creation order until reordering/removal shifts them.
 * Tracks render bottom-up, so a later track sits above an earlier one. */
uint32_t jfx_timeline_add_track(jfx_timeline_t *timeline, const char *name);
jfx_result_t jfx_timeline_remove_track(jfx_timeline_t *timeline, uint32_t track);
/* Reorders compositing layers; indices between the endpoints shift. */
jfx_result_t jfx_timeline_move_track(jfx_timeline_t *timeline, uint32_t track, uint32_t to_index);
size_t jfx_timeline_track_count(const jfx_timeline_t *timeline);
const char *jfx_timeline_track_name(const jfx_timeline_t *timeline, uint32_t track);
jfx_result_t jfx_timeline_set_track_name(jfx_timeline_t *timeline, uint32_t track,
    const char *name);
/* A muted track is skipped when rendering; a soloed track is the only one that
 * renders, which is how a UI previews one layer. */
jfx_result_t jfx_timeline_set_track_muted(jfx_timeline_t *timeline, uint32_t track, bool muted);
jfx_result_t jfx_timeline_set_track_solo(jfx_timeline_t *timeline, uint32_t track, bool solo);
bool jfx_timeline_track_muted(const jfx_timeline_t *timeline, uint32_t track);
bool jfx_timeline_track_solo(const jfx_timeline_t *timeline, uint32_t track);
jfx_result_t jfx_timeline_set_track_opacity(jfx_timeline_t *timeline, uint32_t track,
    float opacity);
jfx_result_t jfx_timeline_set_track_blend(jfx_timeline_t *timeline, uint32_t track,
    jfx_blend_mode_t mode);
float jfx_timeline_track_opacity(const jfx_timeline_t *timeline, uint32_t track);
jfx_blend_mode_t jfx_timeline_track_blend(const jfx_timeline_t *timeline, uint32_t track);

/* ---- Clips --------------------------------------------------------------- */

typedef struct {
    const char *name;               /* may be NULL; the source's name is used */
    jfx_clip_source_t source;
    /* Source parameters, interpreted per source kind. The layout is fixed so a
     * document can round-trip it:
     *   JFX_CLIP_SOLID     r g b a
     *   JFX_CLIP_GRADIENT  from.r from.g from.b to.r   (to.g/to.b follow the
     *                      angle in the next two slots, so all six are present)
     *   JFX_CLIP_CHECKER   a.r a.g a.b b.r              (b.g/b.b in the next two)
     *   JFX_CLIP_SWEEP     pattern - - -
     *   JFX_CLIP_IMAGE     unused
     * A gradient and a checker need two colours, so the array is eight wide
     * rather than four. */
    float source_params[8];
    const char *image_path;         /* required for IMAGE/VIDEO, else may be NULL */
    uint64_t start_frame;           /* where the clip sits on the timeline */
    uint64_t length_frames;         /* how long it lasts; must be at least 1 */
    uint64_t in_point;              /* which frame of the source is at start_frame */
    float opacity;
    jfx_blend_mode_t blend_mode;    /* how this clip sits over the track below */
    bool enabled;
} jfx_clip_desc_t;

uint32_t jfx_timeline_add_clip(jfx_timeline_t *timeline, uint32_t track,
    const jfx_clip_desc_t *desc);
/* Clips keep creation order. A removal shifts the ones above it down, the same
 * rule the node graph uses, so a UI can predict the new indices. */
jfx_result_t jfx_timeline_remove_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
size_t jfx_timeline_clip_count(const jfx_timeline_t *timeline, uint32_t track);
const char *jfx_timeline_clip_name(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
jfx_result_t jfx_timeline_set_clip_name(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    const char *name);
/* True when the clip covers `frame`; the first match in creation order wins. */
bool jfx_timeline_clip_covers(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint64_t frame);
/* The index of the clip covering `frame`, or -1. */
int jfx_timeline_clip_at(const jfx_timeline_t *timeline, uint32_t track, uint64_t frame);

jfx_result_t jfx_timeline_set_clip_enabled(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    bool enabled);
jfx_result_t jfx_timeline_set_clip_opacity(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    float opacity);
jfx_result_t jfx_timeline_set_clip_blend(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    jfx_blend_mode_t mode);
/* Moves a clip within its track, which is what a drag in the timeline does. */
jfx_result_t jfx_timeline_move_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t to_index);
/* Moves a clip to a different track, optionally at a position in the new one. */
jfx_result_t jfx_timeline_relocate_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t to_track, uint32_t to_index, bool keep_index);
/* Trims a clip's head or tail, moving `start_frame`/`length_frames` and
 * adjusting `in_point` so the same source frames stay visible. */
jfx_result_t jfx_timeline_trim_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    int64_t new_start, int64_t new_length);
/* Frame-accurate NLE edits, atomic on error. Position keeps the source in-point
 * and carries clip-relative animation with it. Split inserts a deep copy after the left
 * clip, preserving interpolated effects. Duplicate appends an independent copy.
 * Frame ranges and source ranges must fit INT64_MAX. */
jfx_result_t jfx_timeline_position_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t to_track, uint64_t start_frame);
jfx_result_t jfx_timeline_split_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint64_t frame);
jfx_result_t jfx_timeline_duplicate_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t to_track, uint64_t start_frame);
jfx_result_t jfx_timeline_slip_clip(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    int64_t delta_frames);
/* Track-local ripple: delete closes the selected interval, insert opens a gap.
 * Rejects intersecting/straddling clips rather than destructively cutting them. */
jfx_result_t jfx_timeline_ripple_delete(jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
jfx_result_t jfx_timeline_ripple_insert(jfx_timeline_t *timeline, uint32_t track,
    uint64_t frame, uint64_t length_frames);

/* A clip's source and its four source parameters, so a UI can show them and a
 * document can round-trip them. Per source: `solid` is (r, g, b, a);
 * `gradient` is (from.r, from.g, from.b, angle); `checker` is (a.r, a.g, a.b,
 * tile size); `sweep` is (pattern, -, -, -); `image` ignores them. */
jfx_clip_source_t jfx_timeline_clip_source(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip);
const char *jfx_timeline_clip_path(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
const float *jfx_timeline_clip_params(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
bool jfx_timeline_clip_enabled(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
/* The exact rate, so a document can record 30000/1001 rather than 29.97. */
uint32_t jfx_timeline_fps_num(const jfx_timeline_t *timeline);
uint32_t jfx_timeline_fps_den(const jfx_timeline_t *timeline);

uint64_t jfx_timeline_clip_start(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
uint64_t jfx_timeline_clip_length(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
uint64_t jfx_timeline_clip_in_point(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
/* Keys are stored in an original clip-relative reference clock. Head trims and
 * splits advance this offset so even smooth curves keep identical samples.
 * Reference time = timeline frame - clip start + key offset. Used by project
 * interchange; moving/slipping the source does not alter the animation offset. */
int64_t jfx_timeline_clip_key_offset(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
jfx_result_t jfx_timeline_set_clip_key_offset(jfx_timeline_t *timeline, uint32_t track, uint32_t clip, int64_t offset);
float jfx_timeline_clip_opacity(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
jfx_blend_mode_t jfx_timeline_clip_blend(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip);

/* ---- Effects: the layer-based stack -------------------------------------- */

/* An effect is a graph node kind applied to a clip, plus the per-instance state
 * a panel needs: on/off, a blend mode against the stage below, an opacity, and
 * keyframes. The effect stack is ordered; index 0 runs first. */
size_t jfx_timeline_effect_count(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip);
/* Appends an effect of the named graph node kind. Returns
 * JFX_ERROR_INVALID_ARGUMENT for a kind that is not an image transform, since
 * a source or a blend with two inputs cannot be a single stack stage. */
uint32_t jfx_timeline_add_effect(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    const char *kind_name);
jfx_result_t jfx_timeline_remove_effect(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect);
jfx_result_t jfx_timeline_move_effect(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, uint32_t to_index);
const char *jfx_timeline_effect_kind(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect);
/* The kind descriptor, so a panel can build its controls from one place. */
const jfx_node_kind_t *jfx_timeline_effect_kind_desc(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip, uint32_t effect);

jfx_result_t jfx_timeline_set_effect_enabled(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, bool enabled);
bool jfx_timeline_effect_enabled(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect);
jfx_result_t jfx_timeline_set_effect_opacity(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, float opacity);
float jfx_timeline_effect_opacity(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect);
jfx_result_t jfx_timeline_set_effect_blend(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, jfx_blend_mode_t mode);
jfx_blend_mode_t jfx_timeline_effect_blend(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip, uint32_t effect);

/* A parameter's static value, and its value at a frame once keyframes are taken
 * into account. A parameter with no keys returns the static value. */
jfx_result_t jfx_timeline_set_effect_param(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, float value);
float jfx_timeline_effect_param(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param);
/* The keyframed value in the clip's reference clock, interpolated per the
 * effect's interpolation mode (the original getter's convention). */
float jfx_timeline_effect_param_at(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, uint64_t frame);
/* The value at a sequence frame, including split/head-trim animation offset;
 * exactly the clock used by the renderer. */
float jfx_timeline_effect_param_on_timeline(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip, uint32_t effect, size_t param, uint64_t frame);
jfx_result_t jfx_timeline_set_effect_string(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t index, const char *text);
const char *jfx_timeline_effect_string(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t index);

/* ---- Keyframes ----------------------------------------------------------- */

jfx_result_t jfx_timeline_set_effect_interp(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, jfx_interp_t interp);
jfx_interp_t jfx_timeline_effect_interp(const jfx_timeline_t *timeline, uint32_t track,
    uint32_t clip, uint32_t effect);
size_t jfx_timeline_key_count(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param);
/* Adds a key in the clip's reference clock, replacing any key on that frame.
 * Editor effect.key.* commands convert timeline frames to this clock. */
jfx_result_t jfx_timeline_add_key(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, uint64_t frame, float value);
jfx_result_t jfx_timeline_remove_key(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, uint64_t frame);
jfx_result_t jfx_timeline_clear_keys(jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param);
/* Enumerates keys in ascending frame order, so a document writer can emit them
 * without probing frames. Returns false when `index` is past the end. */
bool jfx_timeline_key_by_index(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, size_t index, uint64_t *out_frame, float *out_value);
/* The frame of the first key, for a UI drawing a parameter's curve. */
bool jfx_timeline_first_key(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, uint64_t *out_frame);

/* The key on a frame, or false. */
bool jfx_timeline_key_at(const jfx_timeline_t *timeline, uint32_t track, uint32_t clip,
    uint32_t effect, size_t param, uint64_t frame, float *out_value);

/* ---- Rendering ----------------------------------------------------------- */

/* Renders the frame at `frame` into tightly packed RGBA8 `out_pixels`. Tracks
 * composite from the bottom up; within a track, later clips sit over earlier ones.
 * `time_seconds` is the wall-clock time, used only by the animated source
 * patterns. */
jfx_result_t jfx_timeline_render(const jfx_timeline_t *timeline, uint64_t frame, float time_seconds,
    uint8_t *out_pixels);

/* Renders into a caller-provided image, for a UI preview. */
jfx_result_t jfx_timeline_render_image(const jfx_timeline_t *timeline, uint64_t frame,
    float time_seconds, jfx_image_t *out_image);

/* A one-line summary of a track for a UI header: "3 clips, 0:00:02:04". */
jfx_result_t jfx_timeline_describe_track(const jfx_timeline_t *timeline, uint32_t track,
    char *out_text, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* JFX_TIMELINE_H */
