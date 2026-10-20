// Package geometry provides bounded, renderer-independent triangle meshes.
// Meshes own their slices; transforms return independent, validated results.
package geometry

import (
	"fmt"
	"math"
)

const MaxVertices = 65536
const MaxTriangles = 131072

type Vec3 struct{ X, Y, Z float64 }

func (v Vec3) Add(w Vec3) Vec3      { return Vec3{v.X + w.X, v.Y + w.Y, v.Z + w.Z} }
func (v Vec3) Sub(w Vec3) Vec3      { return Vec3{v.X - w.X, v.Y - w.Y, v.Z - w.Z} }
func (v Vec3) Scale(k float64) Vec3 { return Vec3{v.X * k, v.Y * k, v.Z * k} }
func (v Vec3) Dot(w Vec3) float64   { return v.X*w.X + v.Y*w.Y + v.Z*w.Z }
func (v Vec3) Cross(w Vec3) Vec3 {
	return Vec3{v.Y*w.Z - v.Z*w.Y, v.Z*w.X - v.X*w.Z, v.X*w.Y - v.Y*w.X}
}
func (v Vec3) Length() float64 { return math.Hypot(math.Hypot(v.X, v.Y), v.Z) }
func (v Vec3) finite() bool {
	return !math.IsNaN(v.X) && !math.IsNaN(v.Y) && !math.IsNaN(v.Z) && !math.IsInf(v.X, 0) && !math.IsInf(v.Y, 0) && !math.IsInf(v.Z, 0)
}

type Triangle [3]uint32
type Mesh struct {
	Vertices  []Vec3
	Triangles []Triangle
}
type Bounds struct{ Min, Max Vec3 }

func (b Bounds) Contains(v Vec3) bool {
	return v.finite() && v.X >= b.Min.X && v.X <= b.Max.X && v.Y >= b.Min.Y && v.Y <= b.Max.Y && v.Z >= b.Min.Z && v.Z <= b.Max.Z
}

func (m Mesh) Validate() error {
	if len(m.Vertices) < 3 || len(m.Vertices) > MaxVertices || len(m.Triangles) < 1 || len(m.Triangles) > MaxTriangles {
		return fmt.Errorf("geometry: mesh exceeds vertex/triangle bounds or is empty")
	}
	for i, v := range m.Vertices {
		if !v.finite() || math.Abs(v.X) > 1e9 || math.Abs(v.Y) > 1e9 || math.Abs(v.Z) > 1e9 {
			return fmt.Errorf("geometry: invalid vertex %d", i)
		}
	}
	for i, t := range m.Triangles {
		for _, idx := range t {
			if uint64(idx) >= uint64(len(m.Vertices)) {
				return fmt.Errorf("geometry: triangle %d index out of range", i)
			}
		}
		n := m.Vertices[t[1]].Sub(m.Vertices[t[0]]).Cross(m.Vertices[t[2]].Sub(m.Vertices[t[0]]))
		if !n.finite() || n.Length() == 0 {
			return fmt.Errorf("geometry: degenerate triangle %d", i)
		}
	}
	return nil
}
func (m Mesh) Clone() Mesh {
	return Mesh{append([]Vec3(nil), m.Vertices...), append([]Triangle(nil), m.Triangles...)}
}
func (m Mesh) Bounds() (Bounds, error) {
	if err := m.Validate(); err != nil {
		return Bounds{}, err
	}
	b := Bounds{m.Vertices[0], m.Vertices[0]}
	for _, v := range m.Vertices[1:] {
		b.Min = Vec3{math.Min(b.Min.X, v.X), math.Min(b.Min.Y, v.Y), math.Min(b.Min.Z, v.Z)}
		b.Max = Vec3{math.Max(b.Max.X, v.X), math.Max(b.Max.Y, v.Y), math.Max(b.Max.Z, v.Z)}
	}
	return b, nil
}

// VertexNormals computes area-weighted smooth normals. Unreferenced or exactly
// cancelling incident faces are errors rather than NaN/zero normals.
func (m Mesh) VertexNormals() ([]Vec3, error) {
	if err := m.Validate(); err != nil {
		return nil, err
	}
	normals := make([]Vec3, len(m.Vertices))
	for _, t := range m.Triangles {
		n := m.Vertices[t[1]].Sub(m.Vertices[t[0]]).Cross(m.Vertices[t[2]].Sub(m.Vertices[t[0]]))
		for _, idx := range t {
			normals[idx] = normals[idx].Add(n)
		}
	}
	for i, n := range normals {
		length := n.Length()
		if !n.finite() || length == 0 {
			return nil, fmt.Errorf("geometry: undefined normal at vertex %d", i)
		}
		// Divide components directly: 1/length can overflow for valid tiny
		// triangles whose nonzero area is a subnormal float64.
		normals[i] = Vec3{n.X / length, n.Y / length, n.Z / length}
	}
	return normals, nil
}
