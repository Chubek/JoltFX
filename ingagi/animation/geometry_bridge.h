#ifndef INGAGI_ANIMATION_GEOMETRY_H
#define INGAGI_ANIMATION_GEOMETRY_H
#ifdef __cplusplus
extern "C" {
#endif
/* Synchronous, fixed-size lib2geom kernels. Inputs are finite, degree 1..3.
 * No pointers retained, allocations, global state or library threads. */
void ig_anim_bezier(const double *xy, unsigned degree, double t, double *out);
double ig_anim_segment_distance(double px,double py,double ax,double ay,double bx,double by);
#ifdef __cplusplus
}
#endif
#endif
