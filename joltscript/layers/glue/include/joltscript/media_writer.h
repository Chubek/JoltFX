#ifndef JOLT_MEDIA_WRITER_H
#define JOLT_MEDIA_WRITER_H
#include "joltscript/vm.h"
#include <stdbool.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jolt_media_writer jolt_media_writer_t;
typedef struct {
    size_t size;
    const char *container,*video_codec,*audio_codec;
    uint32_t width,height,fps_num,fps_den,sample_rate;
    bool audio;
    uint64_t video_bitrate,audio_bitrate;
} jolt_media_options_t;
bool jolt_media_available(void);
bool jolt_media_codec_available(const char *name,bool audio);
/* Caller owns FILE, which must support 64-bit seek. No protocol IO or threads. */
jolt_status_t jolt_media_writer_open(FILE *file,const jolt_media_options_t *options,jolt_media_writer_t **out);
jolt_status_t jolt_media_writer_video(jolt_media_writer_t *writer,const uint8_t *rgba);
jolt_status_t jolt_media_writer_audio(jolt_media_writer_t *writer,const float *stereo,size_t frames);
jolt_status_t jolt_media_writer_finish(jolt_media_writer_t *writer);
void jolt_media_writer_close(jolt_media_writer_t *writer);
#ifdef __cplusplus
}
#endif
#endif
