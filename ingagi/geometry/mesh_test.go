package geometry

import (
	"math"
	"testing"
)

func TestMeshPrimitives(t *testing.T) {
	for _, build := range []func() (Mesh, error){func() (Mesh, error) { return Cube(2) }, func() (Mesh, error) { return Plane(2, 3) }, func() (Mesh, error) { return UVSphere(1, 16, 8) }} {
		m, err := build()
		if err != nil {
			t.Fatal(err)
		}
		if err = m.Validate(); err != nil {
			t.Fatal(err)
		}
		normals, err := m.VertexNormals()
		if err != nil || len(normals) != len(m.Vertices) {
			t.Fatalf("normals: %v", err)
		}
		for _, n := range normals {
			if math.Abs(n.Length()-1) > 1e-10 {
				t.Fatalf("normal %v", n)
			}
		}
		box, err := m.Bounds()
		if err != nil || !box.Contains(m.Vertices[0]) {
			t.Fatalf("bounds: %v", err)
		}
		copy := m.Clone()
		copy.Vertices[0].X += 1
		if copy.Vertices[0] == m.Vertices[0] {
			t.Fatal("clone aliases source")
		}
	}
	for _, s := range []float64{0, -1, math.NaN(), math.Inf(1)} {
		if _, err := Cube(s); err == nil {
			t.Fatal("invalid cube")
		}
		if _, err := UVSphere(s, 16, 8); err == nil {
			t.Fatal("invalid sphere")
		}
	}
	if _, err := UVSphere(1, 2, 8); err == nil {
		t.Fatal("accepted too few segments")
	}
	if _, err := UVSphere(1, 1000000, 1000000); err == nil {
		t.Fatal("unbounded tessellation")
	}
}

func TestMeshValidationAndDeform(t *testing.T) {
	tiny, err := Plane(1e-160, 1e-160)
	if err != nil {
		t.Fatal(err)
	}
	normals, err := tiny.VertexNormals()
	if err != nil {
		t.Fatal(err)
	}
	for _, n := range normals {
		if !n.finite() || n.Y != 1 {
			t.Fatalf("tiny normal: %v", n)
		}
	}
	for _, m := range []Mesh{{}, {Vertices: []Vec3{{}, {X: 1}, {Y: 1}}, Triangles: []Triangle{{0, 1, 3}}}, {Vertices: []Vec3{{}, {X: 1}, {X: 2}}, Triangles: []Triangle{{0, 1, 2}}}} {
		if err := m.Validate(); err == nil {
			t.Fatal("invalid mesh accepted")
		}
	}
	m, _ := Cube(2)
	moved, err := m.Transform(func(v Vec3) (Vec3, error) { return v.Add(Vec3{X: 3}), nil })
	if err != nil || moved.Vertices[0].X != m.Vertices[0].X+3 {
		t.Fatalf("transform: %v", err)
	}
	before := m.Clone()
	if _, err = m.Transform(func(v Vec3) (Vec3, error) { return Vec3{X: math.NaN()}, nil }); err == nil {
		t.Fatal("nonfinite transform")
	}
	for i, v := range m.Vertices {
		if v != before.Vertices[i] {
			t.Fatal("failed transform mutated input")
		}
	}
	if _, err = m.Transform(nil); err == nil {
		t.Fatal("nil transform")
	}
}

func TestClosedTopology(t *testing.T) {
	cube, _ := Cube(2)
	for _, m := range []Mesh{cube, mustSphere(t, 3, 2), mustSphere(t, 16, 8)} {
		type edgeUse struct{ count, direction int }
		edges := map[[2]uint32]edgeUse{}
		volume := 0.
		for _, face := range m.Triangles {
			a, b, c := m.Vertices[face[0]], m.Vertices[face[1]], m.Vertices[face[2]]
			if b.Sub(a).Cross(c.Sub(a)).Dot(a.Add(b).Add(c)) <= 0 {
				t.Fatal("inward face")
			}
			volume += a.Dot(b.Cross(c)) / 6
			for j := 0; j < 3; j++ {
				a, b := face[j], face[(j+1)%3]
				sign := 1
				if a > b {
					a, b = b, a
					sign = -1
				}
				key := [2]uint32{a, b}
				use := edges[key]
				use.count++
				use.direction += sign
				edges[key] = use
			}
		}
		for _, use := range edges {
			if use.count != 2 || use.direction != 0 {
				t.Fatal("non-manifold seam or inconsistent winding")
			}
		}
		if len(m.Vertices)-len(edges)+len(m.Triangles) != 2 || volume <= 0 {
			t.Fatal("not a closed genus-zero surface")
		}
	}
}

func mustSphere(t *testing.T, segments, rings int) Mesh {
	t.Helper()
	m, err := UVSphere(1, segments, rings)
	if err != nil {
		t.Fatal(err)
	}
	return m
}

func FuzzPrimitiveValidation(f *testing.F) {
	f.Add(1., uint8(16), uint8(8))
	f.Fuzz(func(t *testing.T, radius float64, s, r uint8) {
		m, err := UVSphere(radius, 3+int(s%64), 2+int(r%64))
		if err != nil {
			if len(m.Vertices) != 0 || len(m.Triangles) != 0 {
				t.Fatal("published failed mesh")
			}
			return
		}
		if err = m.Validate(); err != nil {
			t.Fatal(err)
		}
		if _, err = m.VertexNormals(); err != nil {
			t.Fatal(err)
		}
	})
}
