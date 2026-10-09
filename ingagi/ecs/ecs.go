// Package ecs is Ingagi's Entity-Component-System core: generational
// entity IDs, schemaless component storage, resources, hierarchies, a
// dependency scheduler and a deferred command buffer.
//
// The package is intentionally dependency-free (no runtime import):
// component payloads are plain Go values, so the game engine can own
// component data with zero coupling to the scripting VM. The VM stores its
// Value structs opaquely as `any` and converts at the boundary.
package ecs

// World owns entities, components, resources and hierarchies.
type World struct {
	gen    []uint32
	alive  []bool
	free   []uint32
	comps  map[string]*Storage
	res    map[string]any
	parent map[EntityID]EntityID
	kids   map[EntityID][]EntityID
}

// NewWorld returns an empty world.
func NewWorld() *World {
	return &World{
		comps:  map[string]*Storage{},
		res:    map[string]any{},
		parent: map[EntityID]EntityID{},
		kids:   map[EntityID][]EntityID{},
	}
}

// Spawn creates an entity with an initial component set (nil for bare).
// Field maps are stored by reference; callers must not retain them.
func (w *World) Spawn(comps map[string]map[string]any) EntityID {
	var idx uint32
	if len(w.free) > 0 {
		idx = w.free[len(w.free)-1]
		w.free = w.free[:len(w.free)-1]
	} else {
		idx = uint32(len(w.gen))
		w.gen = append(w.gen, 0)
		w.alive = append(w.alive, false)
	}
	w.alive[idx] = true
	id := EntityID{Index: idx, Gen: w.gen[idx]}
	for name, fields := range comps {
		w.storage(name).data[idx] = fields
	}
	return id
}

// Despawn destroys an entity, releasing components and hierarchy edges.
// Generations advance so stale handles fail Alive checks.
func (w *World) Despawn(id EntityID) {
	if !w.Alive(id) {
		return
	}
	for _, st := range w.comps {
		delete(st.data, id.Index)
	}
	if p, ok := w.parent[id]; ok {
		delete(w.parent, id)
		w.removeKid(p, id)
	}
	for _, k := range w.kids[id] {
		delete(w.parent, k)
	}
	delete(w.kids, id)
	w.alive[id.Index] = false
	w.gen[id.Index]++
	w.free = append(w.free, id.Index)
}

// Alive reports whether id names a live entity.
func (w *World) Alive(id EntityID) bool {
	return id.Index < uint32(len(w.alive)) && w.alive[id.Index] && w.gen[id.Index] == id.Gen
}

// Count returns the number of live entities.
func (w *World) Count() int {
	n := 0
	for _, a := range w.alive {
		if a {
			n++
		}
	}
	return n
}

// storage returns the store for a component, creating it on demand.
func (w *World) storage(name string) *Storage {
	st, ok := w.comps[name]
	if !ok {
		st = &Storage{data: map[uint32]map[string]any{}}
		w.comps[name] = st
	}
	return st
}

// Add attaches a component (replacing any existing payload).
func (w *World) Add(id EntityID, comp string, fields map[string]any) {
	if !w.Alive(id) {
		return
	}
	if fields == nil {
		fields = map[string]any{}
	}
	w.storage(comp).data[id.Index] = fields
}

// Remove detaches a component.
func (w *World) Remove(id EntityID, comp string) {
	if st, ok := w.comps[comp]; ok {
		delete(st.data, id.Index)
	}
}

// Has reports component attachment.
func (w *World) Has(id EntityID, comp string) bool {
	if !w.Alive(id) {
		return false
	}
	st, ok := w.comps[comp]
	if !ok {
		return false
	}
	_, ok = st.data[id.Index]
	return ok
}

// Get returns a component's field map (nil, false when absent).
func (w *World) Get(id EntityID, comp string) (map[string]any, bool) {
	if !w.Alive(id) {
		return nil, false
	}
	st, ok := w.comps[comp]
	if !ok {
		return nil, false
	}
	f, ok := st.data[id.Index]
	return f, ok
}

// Set replaces a component's fields (adds when absent).
func (w *World) Set(id EntityID, comp string, fields map[string]any) {
	w.Add(id, comp, fields)
}

// ComponentsOf lists attached component names (unsorted).
func (w *World) ComponentsOf(id EntityID) []string {
	var out []string
	for name, st := range w.comps {
		if _, ok := st.data[id.Index]; ok {
			out = append(out, name)
		}
	}
	return out
}

// Query returns live entities carrying all of with and none of without.
func (w *World) Query(with, without []string) []EntityID {
	// Iterate the smallest store for efficiency.
	base := ""
	best := -1
	for _, c := range with {
		n := 0
		if st, ok := w.comps[c]; ok {
			n = len(st.data)
		}
		if best < 0 || n < best {
			best, base = n, c
		}
	}
	var out []EntityID
	consider := func(idx uint32) {
		if !w.alive[idx] {
			return
		}
		id := EntityID{Index: idx, Gen: w.gen[idx]}
		for _, c := range with {
			st, ok := w.comps[c]
			if !ok {
				return
			}
			if _, ok := st.data[idx]; !ok {
				return
			}
		}
		for _, c := range without {
			if st, ok := w.comps[c]; ok {
				if _, ok := st.data[idx]; ok {
					return
				}
			}
		}
		out = append(out, id)
	}
	if base != "" {
		for idx := range w.comps[base].data {
			consider(idx)
		}
		return out
	}
	for idx := range w.alive {
		consider(uint32(idx))
	}
	return out
}

// SetResource stores a world-scoped singleton.
func (w *World) SetResource(name string, v any) { w.res[name] = v }

// GetResource fetches a singleton.
func (w *World) GetResource(name string) (any, bool) {
	v, ok := w.res[name]
	return v, ok
}

// SetParent links child under parent (hierarchy edge only; transforms are
// an engine concern).
func (w *World) SetParent(child, parent EntityID) {
	if !w.Alive(child) || !w.Alive(parent) {
		return
	}
	if old, ok := w.parent[child]; ok {
		w.removeKid(old, child)
	}
	w.parent[child] = parent
	w.kids[parent] = append(w.kids[parent], child)
}

// GetParent returns the parent edge, if any.
func (w *World) GetParent(child EntityID) (EntityID, bool) {
	p, ok := w.parent[child]
	return p, ok
}

// Children returns the child edges of an entity.
func (w *World) Children(parent EntityID) []EntityID {
	return w.kids[parent]
}

func (w *World) removeKid(parent, kid EntityID) {
	ks := w.kids[parent]
	for i, k := range ks {
		if k == kid {
			w.kids[parent] = append(ks[:i], ks[i+1:]...)
			return
		}
	}
}
