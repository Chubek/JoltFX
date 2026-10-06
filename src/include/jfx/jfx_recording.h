#ifndef JFX_RECORDING_H
#define JFX_RECORDING_H
#include "jfx_audio.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_RECORDING_API_MAJOR 1
#define JFX_RECORDING_API_MINOR 0
typedef struct jfx_audio_recording jfx_audio_recording_t;
/* Incremental float32 WAV writer, independent of input device/FFmpeg. Bounded
 * buffers, one owner thread, temporary output atomically published on finish.
 * destroy/cancel remove incomplete output and preserve an existing target. */
jfx_result_t jfx_audio_recording_begin(const char *path,uint32_t sample_rate,jfx_audio_recording_t **out_recording);
jfx_result_t jfx_audio_recording_push(jfx_audio_recording_t *recording,const float *stereo,size_t frames);
uint64_t jfx_audio_recording_frames(const jfx_audio_recording_t *recording);
jfx_result_t jfx_audio_recording_finish(jfx_audio_recording_t *recording);
void jfx_audio_recording_cancel(jfx_audio_recording_t *recording);
void jfx_audio_recording_destroy(jfx_audio_recording_t *recording);
#ifdef __cplusplus
}
#endif
#endif
