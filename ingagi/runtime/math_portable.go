//go:build !cgo

package runtime

func multiplyMat4(a, b Mat4) Mat4 { return scalarMultiplyMat4(a, b) }

// Perspective builds a right-handed projection with depth range [-1, 1].
func Perspective(fovy, aspect, near, far float64) Mat4 {
	return scalarPerspective(fovy, aspect, near, far)
}

// LookAt builds a right-handed view matrix.
func LookAt(eye, center, up Vec3) Mat4 { return scalarLookAt(eye, center, up) }

// AddVec3Batch adds complete triples up to the shortest slice. Exact in-place
// operation is supported; partially overlapping slices are not supported.
func AddVec3Batch(dst, a, b []float64) { scalarAddVec3Batch(dst, a, b) }

// ScaleVec3Batch scales complete triples up to the shortest slice. Exact
// in-place operation is supported; partially overlapping slices are not.
func ScaleVec3Batch(dst, a []float64, s float64) { scalarScaleVec3Batch(dst, a, s) }

// TransformPoints applies m to complete triples with w=1, without perspective
// division. Exact in-place operation is supported; partial overlap is not.
func TransformPoints(dst, src []float64, m Mat4) { scalarTransformPoints(dst, src, m) }
