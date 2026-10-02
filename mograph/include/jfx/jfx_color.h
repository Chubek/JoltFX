#ifndef JFX_COLOR_H
#define JFX_COLOR_H
#include "jfx_compose.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JFX_COLOR_API_MAJOR 1
#define JFX_COLOR_API_MINOR 0
#define JFX_COLOR_API_PATCH 0
typedef enum { JFX_COLOR_NONE=0, JFX_COLOR_CALIBRATION, JFX_COLOR_GRADING } jfx_color_section_t;
/* Immutable kernel-derived descriptors, also enumerated by the node library.
 * kind_at returns NULL for an invalid index; section(NULL) returns NONE. */
size_t jfx_color_kind_count(void);
const jfx_node_kind_t *jfx_color_kind_at(size_t index);
jfx_color_section_t jfx_color_section(const jfx_node_kind_t *kind);
/* Straight RGBA float at this boundary (graph convention). The adapter converts
 * to/from the kernels' premultiplied representation. RGB may exceed [0,1]; alpha
 * must be in [0,1]. Output/aliasing is transactional. LUT is borrowed; NULL means
 * identity for LUT operators. kind must come from color_kind_at or the shared
 * node catalog. Dimensions are limited to 4096 in each axis. CPU execution has
 * a 512 MiB scratch budget and bounded work/pixel; compilation is per call. */
jfx_result_t jfx_color_apply(const jfx_node_kind_t *kind, const jfx_node_value_t *value,
    const jfx_lut_t *lut, const float *src, size_t width, size_t height, float *out);
/* JSON metadata for native/JS/mobile/host widgets. Includes section, stable IDs,
 * parameter ranges/defaults and path fields. Returns OUT_OF_MEMORY if too small. */
jfx_result_t jfx_color_catalog(char *out, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
