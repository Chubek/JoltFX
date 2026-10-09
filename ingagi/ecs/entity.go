package ecs

// EntityID is a generational handle: Index locates the slot, Gen rejects
// stale references after slot reuse. The zero value is the null entity.
type EntityID struct {
	Index uint32
	Gen   uint32
}

// NullEntity is the absence of an entity.
var NullEntity = EntityID{Index: ^uint32(0)}

// IsNull reports the null entity.
func (e EntityID) IsNull() bool { return e == NullEntity }
