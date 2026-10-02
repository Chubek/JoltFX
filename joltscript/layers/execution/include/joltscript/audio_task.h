#ifndef JOLT_AUDIO_TASK_H
#define JOLT_AUDIO_TASK_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Runs a validated 8-input/4-output JBC1 kernel over stereo samples. Binding:
 * left,right,accumulated_left,accumulated_right,gain,pan,fade_in,fade_out.
 * Clock normalization is double precision; lanes 0/1 accumulate unclipped.
 * Output is published only on success, including aliased buffers. */
jolt_status_t jolt_audio_accumulate(const uint8_t *code,size_t code_size,
    const float *src, size_t frames, float gain,
    float pan, int64_t reference_sample, uint64_t reference_length,
    uint64_t fade_in, uint64_t fade_out, float *out);
#ifdef __cplusplus
}
#endif
#endif
