#ifndef JFX_FILAMENT_RENDERER_H
#define JFX_FILAMENT_RENDERER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    const float *vertices; /* interleaved object-space position/normal, 6 floats */
    uint32_t vertex_count;
    float model[16], color[3];
} jfx_filament_draw_t;
typedef struct {
    size_t size;
    const jfx_filament_draw_t *draws;
    uint32_t draw_count, width, height;
    float eye[3], target[3], up[3], vertical_fov;
    void *(*allocate)(size_t);
    void (*release)(void *);
} jfx_filament_desc_t;
/* Private synchronous C bridge, owned inputs/temporary output live through
 * return. Filament SDK types and C++20 stay in this separately built library. */
int jfx_filament_render(const jfx_filament_desc_t *desc,uint8_t *out_rgba,size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
