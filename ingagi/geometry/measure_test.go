package geometry

import (
	"math"
	"testing"
)

func TestTriangleBackendParity(t *testing.T) {
	meshes := []Mesh{}
	cube, _ := Cube(2)
	plane, _ := Plane(2, 3)
	sphere, _ := UVSphere(1, 16, 8)
	meshes = append(meshes, cube, plane, sphere, Mesh{Vertices: []Vec3{{0, 0, 0}, {1e-150, 0, 0}, {0, 1e-150, 0}}, Triangles: []Triangle{{0, 1, 2}}})
	for _, m := range meshes {
		got, want := triangleVectors(m), scalarTriangleVectors(m)
		for i := range got {
			if got[i] != want[i] {
				t.Fatalf("face %d: %v != %v", i, got[i], want[i])
			}
		}
		if _, err := m.VertexNormals(); err != nil {
			t.Fatal(err)
		}
	}
	for _, tc := range []struct {
		m    Mesh
		area float64
	}{{cube, 24}, {plane, 6}} {
		a, err := tc.m.Area()
		if err != nil || math.Abs(a-tc.area) > 1e-12 {
			t.Fatalf("area %g: %v", a, err)
		}
	}
}
