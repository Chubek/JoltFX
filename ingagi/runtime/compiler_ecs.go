package runtime

import (
	"github.com/Chubek/JoltFX/ingagi/ecs"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Entity prefabs compile to `prefab::Name` entries taking the world and
// returning the spawned entity. Spawn, hot reload and AOT all funnel
// through the same code path: there is no separate prefab interpreter.

// worldValue wraps a world for argument passing.
func worldValue(w *ecs.World) Value { return Value{K: KWorld, O: w} }

// compilePrefabs emits one entry per entity declaration.
func (c *Compiler) compilePrefabs(f *fe.File) {
	for _, d := range f.Decls {
		if e, ok := d.(*fe.EntityDecl); ok {
			c.compilePrefab(e)
		}
	}
}

func (c *Compiler) compilePrefab(e *fe.EntityDecl) {
	qual := "prefab::" + e.Name
	c.b.MarkEntry(qual)
	c.pushScope(true)
	c.funcName = qual
	c.nilFix = nil
	c.params[qual] = 1
	wslot := c.declare("world")
	eslot := c.declareHidden()
	// entity = spawn(world)
	c.b.Emit(OpLoadLocal, int32(wslot), 0)
	c.b.Emit(OpCall, 1, c.pool.InternString("spawn"))
	c.b.Emit(OpStoreLocal, int32(eslot), 0)
	// Attach components: add_component(world, entity, name, struct).
	for _, a := range e.Attachments {
		c.b.Emit(OpLoadLocal, int32(wslot), 0)
		c.b.Emit(OpLoadLocal, int32(eslot), 0)
		c.b.Emit(OpPushString, 0, c.pool.InternString(a.Type.Last()))
		for _, in := range a.Inits {
			c.b.Emit(OpPushString, 0, c.pool.InternString(in.Field))
			if in.Value != nil {
				c.compileExpr(in.Value)
			} else {
				c.compileIdentLoad(in.Field, in.Span)
			}
		}
		c.b.Emit(OpMakeStruct, int32(len(a.Inits)), c.pool.InternString(a.Type.Last()))
		c.b.Emit(OpCall, 4, c.pool.InternString("add_component"))
		c.b.Emit(OpPop, 0, 0)
	}
	// Children evaluate (struct or entity) and link to the parent.
	for _, ch := range e.Children {
		c.b.Emit(OpLoadLocal, int32(wslot), 0)
		c.compileExpr(ch.Value)
		c.b.Emit(OpLoadLocal, int32(eslot), 0)
		c.b.Emit(OpCall, 3, c.pool.InternString("set_parent"))
		c.b.Emit(OpPop, 0, 0)
	}
	c.b.Emit(OpLoadLocal, int32(eslot), 0)
	c.b.Emit(OpReturn, 0, 0)
	for _, at := range c.nilFix {
		c.b.Patch(at, c.b.Pos())
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	c.nilFix = nil
	c.popScope()
}

// spawnPrefab runs a prefab entry against a world.
func (vm *VM) spawnPrefab(w *ecs.World, name string) (Value, error) {
	pc, ok := vm.funcs["prefab::"+name]
	if !ok {
		return NilValue, vm.fail("unknown prefab %q", name)
	}
	return vm.callEntry(pc, "prefab::"+name, nil, []Value{worldValue(w)}, vm.budget)
}

// spawnPrefabValue instantiates an entity-typed struct value: struct fields
// naming components attach directly; nested entity structs spawn as
// children linked to the new entity.
func (vm *VM) spawnPrefabValue(w *ecs.World, v Value) (Value, error) {
	s := v.O.(*StructObj)
	id := w.Spawn(nil)
	eid := EntityRef{Index: id.Index, Gen: id.Gen}
	for k, fv := range s.Fields {
		sub, ok := fv.O.(*StructObj)
		if !ok {
			continue
		}
		if _, isEnt := vm.prefabs[sub.Type]; isEnt {
			child, err := vm.spawnPrefabValue(w, fv)
			if err != nil {
				return NilValue, err
			}
			if cerr := vm.linkParent(w, child, entityValue(id)); cerr != nil {
				return NilValue, cerr
			}
			_ = eid
			continue
		}
		w.Add(id, k, flatten(sub.Fields))
		_ = vm.fireObservers(0, k, fv, vm.budget)
	}
	return entityValue(id), nil
}

// linkParent records a hierarchy edge.
func (vm *VM) linkParent(w *ecs.World, child, parent Value) error {
	cid, err := entityArg(child)
	if err != nil {
		return err
	}
	pid, err := entityArg(parent)
	if err != nil {
		return err
	}
	w.SetParent(cid, pid)
	return nil
}
