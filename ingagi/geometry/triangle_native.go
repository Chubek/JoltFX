//go:build cgo

package geometry

/*
#cgo CXXFLAGS: -std=c++20 -DEIGEN_DONT_PARALLELIZE -I${SRCDIR}/../../third_party/libigl/include -isystem ${SRCDIR}/../../third_party/bullet3/examples/ThirdPartyLibs
#cgo linux LDFLAGS: -lstdc++ -lm
#cgo darwin LDFLAGS: -lc++
#cgo windows LDFLAGS: -lstdc++
#include <stddef.h>
#include <stdint.h>
void ingagi_triangle_vectors(double *, const double *, const uint32_t *, size_t);
*/
import "C"

import "unsafe"

// Called only after Mesh.Validate has established nonempty, bounded storage.
func triangleVectors(m Mesh) []Vec3 {
	vertices := make([]float64, 3*len(m.Vertices))
	faces := make([]uint32, 3*len(m.Triangles))
	out := make([]float64, len(faces))
	for i, v := range m.Vertices {
		vertices[3*i], vertices[3*i+1], vertices[3*i+2] = v.X, v.Y, v.Z
	}
	for i, f := range m.Triangles {
		copy(faces[3*i:], f[:])
	}
	C.ingagi_triangle_vectors((*C.double)(unsafe.Pointer(&out[0])),
		(*C.double)(unsafe.Pointer(&vertices[0])), (*C.uint32_t)(unsafe.Pointer(&faces[0])), C.size_t(len(m.Triangles)))
	result := make([]Vec3, len(m.Triangles))
	for i := range result {
		result[i] = Vec3{out[3*i], out[3*i+1], out[3*i+2]}
	}
	return result
}
