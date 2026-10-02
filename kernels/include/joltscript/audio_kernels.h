#ifndef JOLT_AUDIO_KERNELS_H
#define JOLT_AUDIO_KERNELS_H
#include "joltscript/compiler.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Compile the embedded audio_mix source through Glue for the audio executor. */
jolt_status_t jolt_audio_mix_compile(jolt_program_t **out_program);
#ifdef __cplusplus
}
#endif
#endif
