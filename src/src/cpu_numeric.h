#ifndef JFX_CPU_NUMERIC_H
#define JFX_CPU_NUMERIC_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Private, allocation-free kernels. Buffers may be unaligned. Float operations
 * support exact in-place use; partial overlap is not supported. */
void jfx_cpu_accumulate(float *dst,const float *src,size_t count,float gain,float master);
void jfx_cpu_exposure(float *dst,const float *src,size_t pixels,float gain);
void jfx_cpu_decode_unorm(float *dst,const uint8_t *src,size_t count);
bool jfx_cpu_finite(const float *src,size_t count);
/* Build-time scalar switch is for portability and reference benchmarking. */
size_t jfx_cpu_lanes(void);
#ifdef __cplusplus
}
#endif
#endif
