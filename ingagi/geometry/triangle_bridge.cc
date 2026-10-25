//go:build cgo

#include <igl/doublearea.h>
#include <cstddef>
#include <cstdint>

// Projected signed double areas are the components of each oriented triangle's
// area vector. Fixed-size Eigen values stay on the stack; no library threads or
// retained pointers. Go validates indices and owns input/output storage.
extern "C" void ingagi_triangle_vectors(double *out, const double *vertices,
                                        const uint32_t *faces, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        for (int d = 0; d < 3; ++d) {
            const int x = (d + 1) % 3, y = (d + 2) % 3;
            const double *a = vertices + 3 * faces[3 * i];
            const double *b = vertices + 3 * faces[3 * i + 1];
            const double *c = vertices + 3 * faces[3 * i + 2];
            out[3 * i + d] = igl::doublearea_single(
                Eigen::Vector2d(a[x], a[y]), Eigen::Vector2d(b[x], b[y]),
                Eigen::Vector2d(c[x], c[y]));
        }
    }
}
