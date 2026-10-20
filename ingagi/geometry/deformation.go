package geometry

import "fmt"

// Transform maps vertices on an owned clone. Failure discards the candidate;
// callbacks must not mutate the source mesh or change its topology concurrently.
func (m Mesh) Transform(fn func(Vec3) (Vec3, error)) (Mesh, error) {
	if fn == nil {
		return Mesh{}, fmt.Errorf("geometry: nil vertex transform")
	}
	if err := m.Validate(); err != nil {
		return Mesh{}, err
	}
	result := m.Clone()
	for i, v := range m.Vertices {
		next, err := fn(v)
		if err != nil {
			return Mesh{}, fmt.Errorf("geometry: vertex %d: %w", i, err)
		}
		result.Vertices[i] = next
	}
	if err := result.Validate(); err != nil {
		return Mesh{}, err
	}
	return result, nil
}
