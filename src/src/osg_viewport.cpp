#include "jfx/jfx_osg_viewport.h"
#include <cmath>
#include <cstring>
#ifdef JFX_HAVE_OSG
/* Optional OSG math path. Only Vec3/Quat are used so the bridge does not
 * pull in osgViewer, osgDB or OpenGL. Full viewer integration stays opt-in. */
#include <osg/Quat>
#include <osg/Vec3>
#endif
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

static bool finite_float(float v) { return std::isfinite(v); }
static bool finite_double(double v) { return std::isfinite(v); }
static constexpr double kPi = 3.14159265358979323846;

extern "C" jfx_result_t jfx_osg_trackball_orbit(const float in_xyzw[4],
    double dx_deg, double dy_deg, double roll_deg, float out_xyzw[4]) {
    if (!in_xyzw || !out_xyzw) return JFX_ERROR_INVALID_ARGUMENT;
    if (!finite_double(dx_deg) || !finite_double(dy_deg) || !finite_double(roll_deg))
        return JFX_ERROR_INVALID_ARGUMENT;
    for (int i = 0; i < 4; ++i)
        if (!finite_float(in_xyzw[i])) return JFX_ERROR_INVALID_ARGUMENT;
    if (std::fabs(dx_deg) > 90.0 || std::fabs(dy_deg) > 90.0 || std::fabs(roll_deg) > 90.0)
        return JFX_ERROR_INVALID_ARGUMENT;
#ifdef JFX_HAVE_OSG
    float norm_sq = in_xyzw[0] * in_xyzw[0] + in_xyzw[1] * in_xyzw[1] +
        in_xyzw[2] * in_xyzw[2] + in_xyzw[3] * in_xyzw[3];
    if (std::fabs(norm_sq - 1.0f) > 0.01f) return JFX_ERROR_INVALID_ARGUMENT;
    osg::Quat q(in_xyzw[0], in_xyzw[1], in_xyzw[2], in_xyzw[3]);
    osg::Quat yaw(dx_deg * kPi / 180.0, osg::Vec3(0, 1, 0));
    osg::Quat pitch(dy_deg * kPi / 180.0, osg::Vec3(1, 0, 0));
    osg::Quat roll(roll_deg * kPi / 180.0, osg::Vec3(0, 0, 1));
    /* osg::Quat composes factors in transpose order relative to GLM, so
     * the GLM form q * (roll * yaw * pitch) is written reversed here;
     * single-axis results are identical either way. */
    osg::Quat result = (pitch * yaw * roll) * q;
    /* osg::Quat has no normalize(); axis-angle products of a unit input
     * stay unit up to float drift, removed here explicitly. */
    double rx = result.x(), ry = result.y(), rz = result.z(), rw = result.w();
    double inv = 1.0 / std::sqrt(rx * rx + ry * ry + rz * rz + rw * rw);
    out_xyzw[0] = float(rx * inv);
    out_xyzw[1] = float(ry * inv);
    out_xyzw[2] = float(rz * inv);
    out_xyzw[3] = float(rw * inv);
#else
    glm::quat q(in_xyzw[3], in_xyzw[0], in_xyzw[1], in_xyzw[2]);
    if (std::fabs(glm::dot(q, q) - 1.0f) > 0.01f) return JFX_ERROR_INVALID_ARGUMENT;
    glm::quat delta = glm::angleAxis(glm::radians(float(roll_deg)), glm::vec3(0, 0, 1)) *
        glm::angleAxis(glm::radians(float(dx_deg)), glm::vec3(0, 1, 0)) *
        glm::angleAxis(glm::radians(float(dy_deg)), glm::vec3(1, 0, 0));
    glm::quat result = glm::normalize(q * delta);
    out_xyzw[0] = result.x;
    out_xyzw[1] = result.y;
    out_xyzw[2] = result.z;
    out_xyzw[3] = result.w;
#endif
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_osg_axis_orbit(const float in_xyzw[4],
    const float axis_xyz[3], double angle_deg, float out_xyzw[4]) {
    if (!in_xyzw || !axis_xyz || !out_xyzw) return JFX_ERROR_INVALID_ARGUMENT;
    if (!finite_double(angle_deg) || std::fabs(angle_deg) > 90.0)
        return JFX_ERROR_INVALID_ARGUMENT;
    for (int i = 0; i < 4; ++i)
        if (!finite_float(in_xyzw[i])) return JFX_ERROR_INVALID_ARGUMENT;
    for (int i = 0; i < 3; ++i)
        if (!finite_float(axis_xyz[i])) return JFX_ERROR_INVALID_ARGUMENT;
    glm::vec3 axis(axis_xyz[0], axis_xyz[1], axis_xyz[2]);
    if (glm::length(axis) < 1e-4f) return JFX_ERROR_INVALID_ARGUMENT;
    axis = glm::normalize(axis);
    glm::quat q(in_xyzw[3], in_xyzw[0], in_xyzw[1], in_xyzw[2]);
    if (std::fabs(glm::dot(q, q) - 1.0f) > 0.01f) return JFX_ERROR_INVALID_ARGUMENT;
    glm::quat result = glm::normalize(
        glm::angleAxis(glm::radians(float(angle_deg)), axis) * q);
    out_xyzw[0] = result.x;
    out_xyzw[1] = result.y;
    out_xyzw[2] = result.z;
    out_xyzw[3] = result.w;
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_osg_damp_velocity(float velocity_deg[3], double damping) {
    if (!velocity_deg) return JFX_ERROR_INVALID_ARGUMENT;
    if (!finite_double(damping) || damping < 0.0 || damping > 1.0)
        return JFX_ERROR_INVALID_ARGUMENT;
    for (int i = 0; i < 3; ++i) {
        if (!finite_float(velocity_deg[i])) return JFX_ERROR_INVALID_ARGUMENT;
        velocity_deg[i] = float(double(velocity_deg[i]) * damping);
        if (std::fabs(velocity_deg[i]) < 1e-4f) velocity_deg[i] = 0.0f;
    }
    return JFX_SUCCESS;
}

extern "C" jfx_result_t jfx_osg_preview_size(int navigating,
    uint32_t want_w, uint32_t want_h, uint32_t *out_w, uint32_t *out_h) {
    if (!out_w || !out_h) return JFX_ERROR_INVALID_ARGUMENT;
    if (!want_w || !want_h || want_w > 1280 || want_h > 1280)
        return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t w = want_w, h = want_h;
    if (navigating) {
        w = w / 2;
        h = h / 2;
        if (w < 160) w = 160;
        if (h < 160) h = 160;
        if (w > want_w) w = want_w;
        if (h > want_h) h = want_h;
    }
    if (!w || !h) return JFX_ERROR_BACKEND_FAILURE;
    *out_w = w;
    *out_h = h;
    return JFX_SUCCESS;
}

extern "C" int jfx_osg_instance_visible(const float center_world[3], float radius,
    const float view_proj[16]) {
    if (!center_world || !view_proj) return 0;
    for (int i = 0; i < 3; ++i)
        if (!finite_float(center_world[i])) return 0;
    if (!finite_float(radius) || radius < 0.0f || radius > 1e6f) return 0;
    for (int i = 0; i < 16; ++i)
        if (!finite_float(view_proj[i])) return 0;
    /* Transform center to clip space (column-major), then test the six
     * planes with a radius margin. Degenerate w is treated as visible so
     * a bad matrix never culls the whole scene. */
    float x = center_world[0], y = center_world[1], z = center_world[2];
    float cx = view_proj[0] * x + view_proj[4] * y + view_proj[8] * z + view_proj[12];
    float cy = view_proj[1] * x + view_proj[5] * y + view_proj[9] * z + view_proj[13];
    float cz = view_proj[2] * x + view_proj[6] * y + view_proj[10] * z + view_proj[14];
    float cw = view_proj[3] * x + view_proj[7] * y + view_proj[11] * z + view_proj[15];
    if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cz) || !std::isfinite(cw))
        return 1;
    if (cw <= 1e-6f) return 1;
    float margin = radius / cw + 0.1f;
    float nx = cx / cw, ny = cy / cw, nz = cz / cw;
    if (nx < -1.0f - margin || nx > 1.0f + margin) return 0;
    if (ny < -1.0f - margin || ny > 1.0f + margin) return 0;
    if (nz < -1.0f - margin || nz > 1.0f + margin) return 0;
    return 1;
}

extern "C" float jfx_osg_gimbal_pick_px(void) { return JFX_OSG_GIMBAL_PICK_PX; }

extern "C" int jfx_osg_have_osg(void) {
#ifdef JFX_HAVE_OSG
    return 1;
#else
    return 0;
#endif
}
