package geometry

import "fmt"

func scalarTriangleVectors(m Mesh) []Vec3 {
	out := make([]Vec3, len(m.Triangles))
	for i, t := range m.Triangles {
		// Same projection origin/order as libigl::doublearea_single.
		out[i] = m.Vertices[t[0]].Sub(m.Vertices[t[2]]).Cross(m.Vertices[t[1]].Sub(m.Vertices[t[2]]))
	}
	return out
}

// Area returns total unsigned surface area, including every triangle once.
func (m Mesh) Area() (float64, error) {
	if err := m.Validate(); err != nil {
		return 0, err
	}
	var area float64
	for _, n := range triangleVectors(m) {
		area += n.Length() / 2
	}
	return area, nil
}

func (b Bounds) Validate() error {
	if !b.Min.finite() || !b.Max.finite() || b.Min.X > b.Max.X || b.Min.Y > b.Max.Y || b.Min.Z > b.Max.Z {
		return fmt.Errorf("geometry: invalid axis-aligned bounds")
	}
	return nil
}
