#ifndef INGAGI_MATH_BRIDGE_H
#define INGAGI_MATH_BRIDGE_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous calls; no pointer is retained and all storage belongs to Go. */
void ingagi_mat4_mul(double *out, const double *a, const double *b);
void ingagi_perspective(double *out, double fovy, double aspect, double near_z, double far_z);
void ingagi_look_at(double *out, const double *eye, const double *center, const double *up);
void ingagi_add_batch(double *dst, const double *a, const double *b, size_t n);
void ingagi_scale_batch(double *dst, const double *a, double scale, size_t n);
void ingagi_transform_points(double *dst, const double *src, const double *matrix, size_t n);
#ifdef __cplusplus
}
#endif
#endif
