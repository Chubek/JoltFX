#ifndef JFX_EXPORT_H
#define JFX_EXPORT_H
#include "jfx_editor.h"
#include "jfx_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_EXPORT_API_MAJOR 1
#define JFX_EXPORT_API_MINOR 1
#define JFX_EXPORT_API_PATCH 0
typedef struct {
    size_t size;
    const char *path;
    const char *container;          /* NULL: infer .mp4/.mov/.mkv/.webm/.wav */
    const char *video_codec;        /* NULL: mpeg4/mp4, prores/mov, ffv1/mkv, libvpx-vp9/webm */
    const char *audio_codec;        /* NULL: aac/mp4, pcm_s16le/mov/mkv, libopus/webm */
    uint32_t width, height;         /* 0: exact project raster */
    uint64_t start_frame, frame_count; /* count 0: remaining sequence */
    uint32_t fps_num, fps_den;      /* graph only; 0: 30/1; sequence retains rate */
    bool audio;                    /* sequence audio, stereo */
    uint32_t sample_rate;           /* 0: 48000, allowed 8000..192000 */
    uint64_t video_bitrate, audio_bitrate; /* 0: defaults */
} jfx_export_options_t;
typedef enum { JFX_EXPORT_RUNNING, JFX_EXPORT_COMPLETE, JFX_EXPORT_CANCELLED, JFX_EXPORT_FAILED } jfx_export_state_t;
typedef struct jfx_export_job jfx_export_job_t;
/* Capability checks reflect the linked FFmpeg build, including external codecs. */
bool jfx_export_available(void);
bool jfx_export_codec_available(const char *codec, bool audio);
/* Owns a document snapshot. Step on the frontend's event loop (or an engine
 * scheduler task); never spawns encoder threads. Target is atomically replaced
 * only after successful codec flush/trailer/close. Errors/cancel remove temp.
 * Graph export requires frame_count and omits audio. Frames run synchronously;
 * jobs and mixers are single-owner-thread and have no implicit worker thread.
 * WAV is audio-only stereo float32, requires a sequence and audio=true, ignores
 * video/codec settings, and works without FFmpeg. RIFF payload is <4 GiB. */
jfx_result_t jfx_export_begin(const jfx_editor_t *editor,
    const jfx_export_options_t *options, jfx_export_job_t **out_job);
jfx_result_t jfx_export_step(jfx_export_job_t *job, uint32_t max_frames);
void jfx_export_cancel(jfx_export_job_t *job);
void jfx_export_destroy(jfx_export_job_t *job);
jfx_export_state_t jfx_export_state(const jfx_export_job_t *job);
uint64_t jfx_export_completed_frames(const jfx_export_job_t *job);
uint64_t jfx_export_total_frames(const jfx_export_job_t *job);
typedef bool (*jfx_media_progress_fn)(void *userdata, uint64_t completed, uint64_t total);
jfx_result_t jfx_editor_export_video(const jfx_editor_t *editor,
    const jfx_export_options_t *options, jfx_media_progress_fn progress, void *userdata);
#ifdef __cplusplus
}
#endif
#endif
