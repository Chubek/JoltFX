#include "joltscript/audio_kernels.h"
#include "audio_source.h"
jolt_status_t jolt_audio_mix_compile(jolt_program_t **out) { return jolt_compile(audio_mix_source,out,NULL); }
