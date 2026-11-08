#ifndef JFX_NLE_H
#define JFX_NLE_H
#include "jfx_export.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_NLE_API_MAJOR 1
#define JFX_NLE_API_MINOR 0
bool jfx_nle_subtitles_available(void);
/* OpenTimelineIO/OpenTime SMPTE formatter, including 29.97/59.94 drop-frame.
 * Unsupported drop-frame rates are rejected. */
jfx_result_t jfx_nle_timecode(uint64_t frame,uint32_t fps_num,uint32_t fps_den,bool drop_frame,char *out_text,size_t capacity);

/* Compile a no-input Joltscript kernel returning up to 256 command vectors:
 * [["clip.name" a b c value "text"] ...]. Only sequence editing commands are
 * accepted. 64 KiB source / 100000 VM steps. One undo step; any error rolls back.
 * Error text is optional. No filesystem access is granted to the script. */
jfx_result_t jfx_nle_run_script(jfx_editor_t *editor,const char *source,
    char *out_error,size_t error_capacity);

/* Relink every clip with an exact matching path, as one undoable edit.
 * Replacement must be a readable local file; source kinds/timing are retained. */
jfx_result_t jfx_nle_relink(jfx_editor_t *editor,const char *old_path,const char *new_path);

typedef struct {
    size_t size;
    double integrated_lufs, sample_peak, clap_seconds, confidence;
    bool clap_detected;
} jfx_nle_audio_analysis_t;
/* Analyze 0.1..60 seconds of finite stereo PCM or the opening of a local file.
 * Files decode at 48 kHz. Analysis is synchronous; schedule off the UI thread.
 * Silence has -infinity LUFS. No/ambiguous clap is a successful measurement
 * with clap_detected=false. Outputs are untouched on errors. */
jfx_result_t jfx_nle_analyze_pcm(const float *stereo,size_t frames,uint32_t rate,jfx_nle_audio_analysis_t *out_analysis);
jfx_result_t jfx_nle_analyze_file(const char *path,double seconds,jfx_nle_audio_analysis_t *out_analysis);
/* Align a target clip using measured source clap times. Accounts for both
 * source in-points, rounds to the sequence frame, rejects negative positions. */
jfx_result_t jfx_nle_sync_clap(jfx_editor_t *editor,uint32_t reference_track,uint32_t reference_clip,
    uint32_t target_track,uint32_t target_clip,double reference_seconds,double target_seconds);

typedef struct {
    size_t size;
    uint32_t width,height,fps_num,fps_den;
    double duration_seconds;
    bool video,audio;
    char format[64],video_codec[64],audio_codec[64];
} jfx_nle_media_info_t;
jfx_result_t jfx_nle_probe(const char *path,jfx_nle_media_info_t *out_info);
const char *jfx_nle_ffmpeg_version(void);
const char *jfx_nle_encoder_at(size_t index,bool audio);
/* Independent format conversion using the FFmpeg-backed snapshot export job.
 * No editor is modified. Default raster/rate/duration come from the source.
 * An explicit fps resamples to a constant frame rate. Source audio is stereo.
 * Uses the existing export step/cancel/progress/destructor functions. */
jfx_result_t jfx_nle_convert_begin(const char *source,const jfx_export_options_t *options,jfx_export_job_t **out_job);
/* Two-frame CPU Joltscript transition: cross_dissolve or morph_cut. Straight
 * RGBA8, <=4096 per axis; output untouched on error, may alias either input.
 * Morph cut is a local small-motion fit, not face-aware optical flow. */
jfx_result_t jfx_nle_transition(const char *kernel,const uint8_t *from,const uint8_t *to,
    uint32_t width,uint32_t height,double progress,uint8_t *out_rgba,size_t capacity);
/* OTIO JSON editorial interchange: Timeline/Stack/Track/Clip/Gap and external
 * media references. Unsupported rendering/mixing state and overlaps are rejected
 * instead of dropped. Import uses the supplied sequence raster/rational rate,
 * requires integral rescaled frames, replaces the document and clears history.
 * JSON is bounded to JFX_PROJECT_MAX_BYTES. No media is opened during import. */
jfx_result_t jfx_nle_export_otio(const jfx_editor_t *editor,char *out_json,size_t capacity,size_t *out_written);
jfx_result_t jfx_nle_import_otio(jfx_editor_t *editor,const char *json,size_t length,
    uint32_t width,uint32_t height,uint32_t fps_num,uint32_t fps_den);

#ifdef __cplusplus
}
#endif
#endif
