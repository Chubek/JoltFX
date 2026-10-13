#ifndef JFX_OSG_VIEWPORT_H
#define JFX_OSG_VIEWPORT_H
/* OSG viewport bridge (API 1.0).
 *
 * Blends `third_party/openscenegraph` into the 3D Modeling & Animation
 * viewport without making a full OSG viewer mandatory.
 *
 * When built with `-DJFX_USE_OSG=ON` and OSG headers available,
 * `JFX_HAVE_OSG` is defined and the bridge uses OSG math types
 * (`osg::Quat`, `osg::Vec3`) for trackball navigation. Otherwise the same
 * entry points run an identical GLM fallback, so headless/CI builds keep
 * working. The full `osgViewer` path remains opt-in; the default stays the
 * Core CPU rasterizer so preview, export and tests stay deterministic.
 *
 * What this bridge contributes even without linking OSG:
 *  - OSG-style trackball orbit (quaternion, axis-angle, damping helper);
 *  - adaptive preview resolution while navigating (half-res drag);
 *  - sphere/frustum instance culling for cloner-heavy scenes;
 *  - gimbal picking tolerance and scene statistics for the UI.
 *
 * All functions validate pointers, never allocate on the hot path beyond
 * caller storage, and never spawn threads. */
#include <stddef.h>
#include <stdint.h>
#include "jfx/jfx_result.h"
#ifdef __cplusplus
extern "C" {
#endif

#define JFX_OSG_VIEWPORT_API_MAJOR 1
#define JFX_OSG_VIEWPORT_API_MINOR 0

/* Applies an OSG trackball-style orbit delta to a normalized XYZW
 * quaternion. `dx_deg`/`dy_deg` are yaw/pitch increments in degrees,
 * `roll_deg` is an optional roll (Alt-drag). Output stays normalized.
 * Returns JFX_ERROR_INVALID_ARGUMENT on null or non-finite input. */
jfx_result_t jfx_osg_trackball_orbit(const float in_xyzw[4],
    double dx_deg, double dy_deg, double roll_deg, float out_xyzw[4]);

/* Axis-constrained orbit used by the colored gimbal rings.
 * `axis_xyz` is the world axis (need not be normalized); `angle_deg` is
 * the increment. Mirrors `osgGA::TrackballManipulator` axis rotation. */
jfx_result_t jfx_osg_axis_orbit(const float in_xyzw[4],
    const float axis_xyz[3], double angle_deg, float out_xyzw[4]);

/* Damps a per-frame orbit velocity toward zero. `velocity_deg` holds
 * yaw/pitch/roll rates; `damping` is 0..1 (0.85 is a good default).
 * Used to smooth the jagged per-event deltas into one per-frame update. */
jfx_result_t jfx_osg_damp_velocity(float velocity_deg[3], double damping);

/* Adaptive preview size. While `navigating` is true the bridge suggests
 * half resolution (clamped to >=160px) so drags stay interactive; otherwise
 * it echoes the requested size clamped to 1280. Never returns 0. */
jfx_result_t jfx_osg_preview_size(int navigating,
    uint32_t want_w, uint32_t want_h, uint32_t *out_w, uint32_t *out_h);

/* Sphere-vs-frustum cull for one cloner instance. `center_world` is the
 * instance origin, `radius` its bounding radius, `view_proj` a 4x4
 * column-major view-projection matrix. Returns 1 when visible, 0 when
 * culled. Null/out-of-range inputs return 0 (treated as culled). */
int jfx_osg_instance_visible(const float center_world[3], float radius,
    const float view_proj[16]);

/* Gimbal picking tolerance in screen pixels. OSG manipulators use a wider
 * pick radius than the legacy 9px; this bridge standardizes 14px. */
#define JFX_OSG_GIMBAL_PICK_PX 14.0f
float jfx_osg_gimbal_pick_px(void);

/* 1 when this build links OSG headers, 0 for the GLM fallback. */
int jfx_osg_have_osg(void);

#ifdef __cplusplus
}
#endif
#endif
