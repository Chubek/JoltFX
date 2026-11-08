#ifndef JOLT_VIDEO_IO_H
#define JOLT_VIDEO_IO_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Local-file video decoding. Returns 0 success, -1 decode failure, -2 when
 * FFmpeg is unavailable. Output is RGBA8; caller provides width*height*4 bytes. */
int jolt_video_io_available(void);
typedef struct {
    size_t size;
    uint32_t width,height,fps_num,fps_den;
    double duration;
    int video,audio;
    char format[64],video_codec[64],audio_codec[64];
} jolt_media_info_t;
int jolt_media_probe(const char *path,jolt_media_info_t *out_info);
const char *jolt_media_ffmpeg_version(void);
/* Enumerate linked encoders, not a hardcoded list. Returns borrowed names. */
const char *jolt_media_encoder_at(size_t index,int audio);
int jolt_video_io_frame(const char *path, double seconds, uint32_t width, uint32_t height,
    uint8_t *out_rgba, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
