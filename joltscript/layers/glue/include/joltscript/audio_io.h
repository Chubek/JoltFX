#ifndef JOLT_AUDIO_IO_H
#define JOLT_AUDIO_IO_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct jolt_audio_reader jolt_audio_reader_t;
#define JOLT_AUDIO_NO_STREAM ((jolt_status_t)1)
/* Output always stereo float at the requested rate. No device IO/threads.
 * NO_STREAM denotes a video container with no audio stream. */
jolt_status_t jolt_audio_reader_open(const char *path, uint32_t rate, jolt_audio_reader_t **out);
void jolt_audio_reader_close(jolt_audio_reader_t *reader);
jolt_status_t jolt_audio_reader_read(jolt_audio_reader_t *reader, uint64_t sample,
    size_t frames, float *out);
#ifdef __cplusplus
}
#endif
#endif
