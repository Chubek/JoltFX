package geometry

import (
	"fmt"
	"math"
)

func validSize(v float64) bool { return v > 0 && v <= 1e9 && !math.IsNaN(v) && !math.IsInf(v, 0) }
func validated(m Mesh) (Mesh, error) {
	if err := m.Validate(); err != nil {
		return Mesh{}, err
	}
	return m, nil
}

// Cube returns a centered, welded, outward-wound unit-topology cube.
func Cube(size float64) (Mesh, error) {
	if !validSize(size) {
		return Mesh{}, fmt.Errorf("geometry: invalid cube size")
	}
	h := size / 2
	m := Mesh{Vertices: []Vec3{{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h}, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}, Triangles: []Triangle{
		{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {3, 7, 6}, {3, 6, 2}, {0, 4, 7}, {0, 7, 3}, {1, 2, 6}, {1, 6, 5},
	}}
	return validated(m)
}

// Plane is centered in XZ, with its two triangles facing +Y.
func Plane(width, depth float64) (Mesh, error) {
	if !validSize(width) || !validSize(depth) {
		return Mesh{}, fmt.Errorf("geometry: invalid plane dimensions")
	}
	w, d := width/2, depth/2
	m := Mesh{[]Vec3{{-w, 0, -d}, {w, 0, -d}, {w, 0, d}, {-w, 0, d}}, []Triangle{{0, 2, 1}, {0, 3, 2}}}
	return validated(m)
}

// UVSphere uses welded seams and shared poles, with no zero-area pole faces.
func UVSphere(radius float64, segments, rings int) (Mesh, error) {
	if !validSize(radius) || segments < 3 || rings < 2 || rings > MaxVertices || segments > (MaxVertices-2)/(rings-1) {
		return Mesh{}, fmt.Errorf("geometry: invalid or over-budget sphere tessellation")
	}
	m := Mesh{Vertices: make([]Vec3, 0, 2+segments*(rings-1)), Triangles: make([]Triangle, 0, 2*segments*(rings-1))}
	m.Vertices = append(m.Vertices, Vec3{Y: radius})
	for j := 1; j < rings; j++ {
		phi := math.Pi * float64(j) / float64(rings)
		for i := 0; i < segments; i++ {
			theta := 2 * math.Pi * float64(i) / float64(segments)
			m.Vertices = append(m.Vertices, Vec3{radius * math.Sin(phi) * math.Cos(theta), radius * math.Cos(phi), radius * math.Sin(phi) * math.Sin(theta)})
		}
	}
	bottom := uint32(len(m.Vertices))
	m.Vertices = append(m.Vertices, Vec3{Y: -radius})
	index := func(j, i int) uint32 { return uint32(1 + (j-1)*segments + i%segments) }
	for i := 0; i < segments; i++ {
		m.Triangles = append(m.Triangles, Triangle{0, index(1, i+1), index(1, i)})
		for j := 1; j < rings-1; j++ {
			a, b, c, d := index(j, i), index(j, i+1), index(j+1, i), index(j+1, i+1)
			m.Triangles = append(m.Triangles, Triangle{a, b, c}, Triangle{b, d, c})
		}
		m.Triangles = append(m.Triangles, Triangle{bottom, index(rings-1, i), index(rings-1, i+1)})
	}
	return validated(m)
}
