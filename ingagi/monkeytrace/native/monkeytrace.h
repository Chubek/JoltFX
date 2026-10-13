#ifndef MONKEYTRACE_NATIVE_H
#define MONKEYTRACE_NATIVE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MONKEYTRACE_NATIVE_ABI_MAJOR 1
#define MONKEYTRACE_NATIVE_ABI_MINOR 0
#define MONKEYTRACE_NATIVE_ABI_PATCH 0
#define MONKEYTRACE_NATIVE_MAX_TRIANGLES 4096u
typedef struct { float xyz[9], albedo[3]; } monkeytrace_triangle_t;
typedef struct {
    size_t size;
    const monkeytrace_triangle_t *triangles;
    uint32_t triangle_count, width, height, samples, bounces, timeout_ms;
    float eye[3], target[3], up[3], vertical_fov, reflectivity;
} monkeytrace_render_desc_t;
enum { MONKEYTRACE_SUCCESS=0, MONKEYTRACE_INVALID_ARGUMENT=1,
    MONKEYTRACE_LIMIT=2, MONKEYTRACE_FAILED=3, MONKEYTRACE_TIMEOUT=4 };
/* Synchronous CPU backend. Inputs/output are borrowed only during the call.
 * Rendering copies triangles into an owned Ingagi ECS snapshot; no C pointer
 * is retained. RGBA8 output is untouched on failure. No compiler tools needed. */
int monkeytrace_render(monkeytrace_render_desc_t *desc, uint8_t *out_rgba, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
