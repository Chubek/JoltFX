#include "jfx/jfx_osg_viewport.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool near(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) < eps;
}

int main() {
    // Null validation.
    assert(jfx_osg_trackball_orbit(nullptr, 1, 0, 0, nullptr) != 0);
    float q[4] = {0, 0, 0, 1}, out[4] = {0, 0, 0, 0};
    float bad[4] = {0, 0, 0, 0};
    assert(jfx_osg_trackball_orbit(bad, 1, 0, 0, out) != 0);
    assert(jfx_osg_trackball_orbit(q, 1000, 0, 0, out) != 0);

    // Identity stays normalized under small orbit.
    assert(jfx_osg_trackball_orbit(q, 10, 5, 2, out) == 0);
    float len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    assert(near(len, 1.0f, 1e-3f));

    // Zero delta is identity.
    assert(jfx_osg_trackball_orbit(q, 0, 0, 0, out) == 0);
    assert(near(out[3], 1.0f));

    // Cross-path equivalence: these GLM-computed references must hold for
    // the osg::Quat path too (JFX_HAVE_OSG), locking the transpose-order
    // composition fix. Tolerance covers float drift only.
    {
        float id[4] = {0, 0, 0, 1}, ref[4];
        assert(jfx_osg_trackball_orbit(id, 10, 5, 2, ref) == 0);
        assert(near(ref[0], 0.0419272f) && near(ref[1], 0.0878179f) &&
            near(ref[2], 0.0135683f) && near(ref[3], 0.9951614f));
        float yax[4] = {0, 1, 0};
        assert(jfx_osg_axis_orbit(id, yax, 45, ref) == 0);
        assert(near(ref[0], 0.0f) && near(ref[1], 0.3826835f) &&
            near(ref[2], 0.0f) && near(ref[3], 0.9238795f));
    }
    // Axis orbit: 90 deg about Y maps +Z toward +X.
    float axis[3] = {0, 1, 0};
    assert(jfx_osg_axis_orbit(q, axis, 90, out) == 0);
    len = std::sqrt(out[0] * out[0] + out[1] * out[1] + out[2] * out[2] + out[3] * out[3]);
    assert(near(len, 1.0f, 1e-3f));
    float zero_axis[3] = {0, 0, 0};
    assert(jfx_osg_axis_orbit(q, zero_axis, 10, out) != 0);

    // Damping.
    float vel[3] = {10, -4, 0.00001f};
    assert(jfx_osg_damp_velocity(vel, 0.5) == 0);
    assert(near(vel[0], 5.0f) && near(vel[1], -2.0f) && vel[2] == 0.0f);
    assert(jfx_osg_damp_velocity(nullptr, 0.5) != 0);
    assert(jfx_osg_damp_velocity(vel, 2.0) != 0);

    // Adaptive preview: half-res while navigating, clamped.
    uint32_t w = 0, h = 0;
    assert(jfx_osg_preview_size(1, 1280, 720, &w, &h) == 0);
    assert(w == 640 && h == 360);
    assert(jfx_osg_preview_size(0, 1280, 720, &w, &h) == 0);
    assert(w == 1280 && h == 720);
    assert(jfx_osg_preview_size(0, 0, 720, &w, &h) != 0);
    assert(jfx_osg_preview_size(0, 2000, 720, &w, &h) != 0);

    // Culling: identity view-proj keeps origin visible, far corner culled.
    float vp[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float center[3] = {0, 0, 0};
    assert(jfx_osg_instance_visible(center, 1.0f, vp) == 1);
    float far[3] = {10, 10, 10};
    assert(jfx_osg_instance_visible(far, 0.1f, vp) == 0);
    assert(jfx_osg_instance_visible(nullptr, 1.0f, vp) == 0);

    // Pick radius + OSG presence flag are sane.
    assert(jfx_osg_gimbal_pick_px() == 14.0f);
    int have = jfx_osg_have_osg();
    assert(have == 0 || have == 1);

    std::printf("osg_viewport ok (have_osg=%d)\n", have);
    return 0;
}
