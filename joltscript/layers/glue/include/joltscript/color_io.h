#ifndef JOLT_COLOR_IO_H
#define JOLT_COLOR_IO_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Optional OpenColorIO bridge. All exceptions are converted to a failure with
 * a bounded diagnostic. Caller owns the RGB lattice; red varies fastest. */
int jolt_color_io_available(void);
size_t jolt_color_io_format_count(void);
const char *jolt_color_io_format(size_t index);
int jolt_color_io_bake(const char *path, unsigned edge, float *rgb,
    char *out_error, size_t capacity);
#ifdef __cplusplus
}
#endif
#endif
