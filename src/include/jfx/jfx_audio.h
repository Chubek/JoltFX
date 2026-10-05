#ifndef JFX_AUDIO_H
#define JFX_AUDIO_H
#include "jfx_timeline.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_AUDIO_API_MAJOR 1
#define JFX_AUDIO_API_MINOR 0
#define JFX_AUDIO_API_PATCH 0
#define JFX_AUDIO_MAX_BLOCK_FRAMES 65536u
typedef struct {
    size_t size;
    bool enabled;
    float gain;                    /* linear [0,16]; independent of opacity */
    float pan;                     /* stereo balance [-1,1], unity at center */
    uint64_t fade_in_frames, fade_out_frames;
    uint64_t reference_frames;      /* original fade clock, preserved by splits */
} jfx_clip_audio_t;
jfx_result_t jfx_timeline_get_clip_audio(const jfx_timeline_t *timeline,
    uint32_t track, uint32_t clip, jfx_clip_audio_t *out_audio);
jfx_result_t jfx_timeline_set_clip_audio(jfx_timeline_t *timeline,
    uint32_t track, uint32_t clip, const jfx_clip_audio_t *audio);
jfx_result_t jfx_timeline_set_track_audio_gain(jfx_timeline_t *timeline, uint32_t track, float gain);
float jfx_timeline_track_audio_gain(const jfx_timeline_t *timeline, uint32_t track);
/* Exact ceil(frame * fps_den * sample_rate / fps_num). Overflow is rejected. */
jfx_result_t jfx_timeline_audio_sample(const jfx_timeline_t *timeline,
    uint64_t frame, uint32_t sample_rate, uint64_t *out_sample);
typedef struct jfx_audio_mixer jfx_audio_mixer_t;
/* Mixer owns an immutable snapshot; safe after editing/destroying the timeline.
 * Snapshot copies timeline settings and media paths, not external file bytes.
 * Single-owner-thread; local files only. WAV/FLAC/MP3 use miniaudio;
 * other containers use FFmpeg.
 * Video without an audio stream is silent. Assigned missing/bad media fails.
 * Decoders open lazily, stream in bounded blocks, and close after clip ends. */
jfx_result_t jfx_audio_mixer_create(const jfx_timeline_t *timeline,
    uint32_t sample_rate, jfx_audio_mixer_t **out_mixer);
void jfx_audio_mixer_destroy(jfx_audio_mixer_t *mixer);
/* Interleaved stereo float, silence in gaps and after EOF. Overlaps sum without
 * clipping (headroom is retained). Output untouched on error; arbitrary seeks
 * and contiguous blocks have the same sequence clock. Capacity is float count. */
jfx_result_t jfx_audio_mixer_render(jfx_audio_mixer_t *mixer, uint64_t start_sample,
    size_t frames, float *out_stereo, size_t capacity);
/* Convenience one-block preview; repeated playback should retain a mixer. */
jfx_result_t jfx_timeline_render_audio(const jfx_timeline_t *timeline,
    uint64_t start_sample, uint32_t sample_rate, size_t frames, float *out_stereo, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
