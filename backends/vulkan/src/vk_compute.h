#ifndef JFX_VK_COMPUTE_H
#define JFX_VK_COMPUTE_H

#include <stddef.h>
#include <stdint.h>

typedef struct jvk_compute_device jvk_compute_device_t;

/* Creates the instance, picks a physical device (discrete GPU preferred),
 * and opens a compute queue. Always writes name/api_version, even on
 * failure (then "cpu-fallback"/0). Returns NULL when no usable driver or
 * device exists; the caller keeps the CPU fallback. */
jvk_compute_device_t *jvk_device_create(char *name, size_t name_size,
    uint32_t *api_version);
void jvk_device_destroy(jvk_compute_device_t *device);

/* Runs validated JBC1 bytecode (4 RGBA outputs) over interleaved pixels on
 * the device. Returns 0 on success, -1 on driver/dispatch failure, -2 on
 * numeric failure (non-finite input or output), -3 on budget overrun.
 * `out` is only written on success. */
int jvk_compute_run(jvk_compute_device_t *device, const uint8_t *code,
    size_t size, const float *input_rgba, size_t pixels,
    const float *parameters, size_t parameter_count, float *output_rgba,
    size_t memory_limit);

#endif
