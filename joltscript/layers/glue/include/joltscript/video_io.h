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
int jolt_video_io_frame(const char *path, double seconds, uint32_t width, uint32_t height,
    uint8_t *out_rgba, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
