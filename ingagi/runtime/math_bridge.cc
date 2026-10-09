//go:build cgo

#include "math_bridge.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <simdette/math/linalg.hpp>
#include <algorithm>

extern "C" void ingagi_mat4_mul(double *out, const double *a, const double *b) {
    const auto m = glm::make_mat4(a) * glm::make_mat4(b);
    std::copy_n(glm::value_ptr(m), 16, out);
}

extern "C" void ingagi_perspective(double *out, double fovy, double aspect, double near_z, double far_z) {
    const auto m = glm::perspectiveRH_NO(fovy, aspect, near_z, far_z);
    std::copy_n(glm::value_ptr(m), 16, out);
}

extern "C" void ingagi_look_at(double *out, const double *eye, const double *center, const double *up) {
    const auto m = glm::lookAtRH(glm::make_vec3(eye), glm::make_vec3(center), glm::make_vec3(up));
    std::copy_n(glm::value_ptr(m), 16, out);
}

extern "C" void ingagi_add_batch(double *dst, const double *a, const double *b, size_t n) {
    // Hardware batches currently implement float, not double. Use simdette's
    // scalar linear algebra types to preserve the VM's float64 precision.
    for (size_t i = 0; i < n; i += 3) {
        const auto sum = simdette::vec3<double>{a[i], a[i+1], a[i+2]}
                       + simdette::vec3<double>{b[i], b[i+1], b[i+2]};
        dst[i] = sum.x;
        dst[i+1] = sum.y;
        dst[i+2] = sum.z;
    }
}

extern "C" void ingagi_scale_batch(double *dst, const double *a, double scale, size_t n) {
    for (size_t i = 0; i < n; i += 3) {
        const auto product = simdette::vec3<double>{a[i], a[i+1], a[i+2]} * scale;
        dst[i] = product.x;
        dst[i+1] = product.y;
        dst[i+2] = product.z;
    }
}

extern "C" void ingagi_transform_points(double *dst, const double *src, const double *matrix, size_t n) {
    const auto m = glm::make_mat4(matrix);
    for (size_t i = 0; i < n; i += 3) {
        const auto point = m * glm::dvec4(src[i], src[i+1], src[i+2], 1.0);
        dst[i] = point.x;
        dst[i+1] = point.y;
        dst[i+2] = point.z;
    }
}
