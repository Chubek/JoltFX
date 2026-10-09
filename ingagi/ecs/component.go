package ecs

// Storage is one component's column: entity index to field map.
type Storage struct {
	data map[uint32]map[string]any
}

// Len returns the attached-entity count.
func (s *Storage) Len() int { return len(s.data) }

// Registry tracks declared component names for reflection and tooling.
// Payload schemas live with the declaring language (TypeInfo in runtime);
// the registry stays intentionally schemaless.
type Registry struct {
	names map[string]bool
}

// NewRegistry returns an empty registry.
func NewRegistry() *Registry { return &Registry{names: map[string]bool{}} }

// Declare registers a component name (idempotent).
func (r *Registry) Declare(name string) { r.names[name] = true }

// Declared reports registration.
func (r *Registry) Declared(name string) bool { return r.names[name] }

// Names lists registered components (unsorted).
func (r *Registry) Names() []string {
	out := make([]string, 0, len(r.names))
	for n := range r.names {
		out = append(out, n)
	}
	return out
}
