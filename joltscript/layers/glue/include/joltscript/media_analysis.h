#ifndef JOLT_MEDIA_ANALYSIS_H
#define JOLT_MEDIA_ANALYSIS_H
#include "joltscript/vm.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Finite interleaved stereo, at most 60 seconds. LUFS may be -infinity for
 * silence. Onset is a sample index or UINT64_MAX when no isolated clap exists. */
jolt_status_t jolt_media_analyze(const float *pcm,size_t frames,uint32_t rate,
    double *lufs,double *peak,uint64_t *onset,double *confidence);
#ifdef __cplusplus
}
#endif
#endif
