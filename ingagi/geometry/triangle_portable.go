//go:build !cgo

package geometry

func triangleVectors(m Mesh) []Vec3 { return scalarTriangleVectors(m) }
