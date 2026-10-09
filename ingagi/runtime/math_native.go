//go:build cgo

package runtime

/*
#cgo CXXFLAGS: -std=c++20 -I${SRCDIR}/../../third_party/glm -I${SRCDIR}/../../third_party/simdette/include
#cgo linux LDFLAGS: -lstdc++ -lm
#cgo darwin LDFLAGS: -lc++
#cgo windows LDFLAGS: -lstdc++
#include "math_bridge.h"
*/
import "C"

import "unsafe"

func mathPtr(v []float64) *C.double { return (*C.double)(unsafe.Pointer(&v[0])) }

func multiplyMat4(a, b Mat4) (out Mat4) {
	C.ingagi_mat4_mul(mathPtr(out[:]), mathPtr(a[:]), mathPtr(b[:]))
	return
}

// Perspective builds a right-handed projection with depth range [-1, 1].
func Perspective(fovy, aspect, near, far float64) (out Mat4) {
	C.ingagi_perspective(mathPtr(out[:]), C.double(fovy), C.double(aspect), C.double(near), C.double(far))
	return
}

// LookAt builds a right-handed view matrix.
func LookAt(eye, center, up Vec3) (out Mat4) {
	e, c, u := []float64{eye.X, eye.Y, eye.Z}, []float64{center.X, center.Y, center.Z}, []float64{up.X, up.Y, up.Z}
	C.ingagi_look_at(mathPtr(out[:]), mathPtr(e), mathPtr(c), mathPtr(u))
	return
}

// AddVec3Batch adds complete triples up to the shortest slice. Exact in-place
// operation is supported; partially overlapping slices are not supported.
func AddVec3Batch(dst, a, b []float64) {
	n := min(len(dst), len(a), len(b)) / 3 * 3
	if n > 0 {
		C.ingagi_add_batch(mathPtr(dst), mathPtr(a), mathPtr(b), C.size_t(n))
	}
}

// ScaleVec3Batch scales complete triples up to the shortest slice. Exact
// in-place operation is supported; partially overlapping slices are not.
func ScaleVec3Batch(dst, a []float64, s float64) {
	n := min(len(dst), len(a)) / 3 * 3
	if n > 0 {
		C.ingagi_scale_batch(mathPtr(dst), mathPtr(a), C.double(s), C.size_t(n))
	}
}

// TransformPoints applies m to complete triples with w=1, without perspective
// division. Exact in-place operation is supported; partial overlap is not.
func TransformPoints(dst, src []float64, m Mat4) {
	n := min(len(dst), len(src)) / 3 * 3
	if n > 0 {
		C.ingagi_transform_points(mathPtr(dst), mathPtr(src), mathPtr(m[:]), C.size_t(n))
	}
}
