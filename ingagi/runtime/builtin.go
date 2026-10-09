package runtime

import (
	"fmt"
	"math"

	"github.com/Chubek/JoltFX/ingagi/ecs"
)

// Standard builtins. Every builtin is a plain Go function on Values, so the
// engine can reuse them from host code and native extensions register
// alongside with RegisterHostFn. World-taking builtins default to the VM's
// world when the first argument is omitted.

func registerBuiltins(vm *VM) {
	vm.builtins["print"] = bPrint
	vm.builtins["len"] = bLen
	vm.builtins["type"] = bType
	vm.builtins["keys"] = bKeys
	vm.builtins["push"] = bPush
	vm.builtins["pop"] = bPop
	vm.builtins["range"] = bRange
	vm.builtins["str"] = bStr
	vm.builtins["int"] = bInt
	vm.builtins["float"] = bFloat
	vm.builtins["assert"] = bAssert
	vm.builtins["sin"] = math1("sin", math.Sin)
	vm.builtins["cos"] = math1("cos", math.Cos)
	vm.builtins["tan"] = math1("tan", math.Tan)
	vm.builtins["sqrt"] = math1("sqrt", math.Sqrt)
	vm.builtins["exp"] = math1("exp", math.Exp)
	vm.builtins["log"] = math1("log", math.Log)
	vm.builtins["floor"] = math1("floor", math.Floor)
	vm.builtins["ceil"] = math1("ceil", math.Ceil)
	vm.builtins["abs"] = bAbs
	vm.builtins["min"] = bMin
	vm.builtins["max"] = bMax
	vm.builtins["pow"] = bPow
	vm.builtins["vec2"] = bVec2
	vm.builtins["vec3"] = bVec3
	vm.builtins["vec4"] = bVec4
	vm.builtins["quat"] = bQuat
	vm.builtins["mat4_identity"] = bMat4Identity
	vm.builtins["mat4_translate"] = bMat4Translate
	vm.builtins["mat4_scale"] = bMat4Scale
	vm.builtins["mat4_rotate"] = bMat4Rotate
	vm.builtins["mat4_perspective"] = bMat4Perspective
	vm.builtins["mat4_lookat"] = bMat4LookAt
	vm.builtins["dot"] = bDot
	vm.builtins["cross"] = bCross
	vm.builtins["normalize"] = bNormalize
	vm.builtins["lerp"] = bLerp
	vm.builtins["spawn"] = bSpawn
	vm.builtins["despawn"] = bDespawn
	vm.builtins["add_component"] = bAddComponent
	vm.builtins["remove_component"] = bRemoveComponent
	vm.builtins["has_component"] = bHasComponent
	vm.builtins["get_component"] = bGetComponent
	vm.builtins["set_resource"] = bSetResource
	vm.builtins["get_resource"] = bGetResource
	vm.builtins["set_parent"] = bSetParent
	vm.builtins["get_parent"] = bGetParent
}

func bPrint(vm *VM, args []Value) (Value, error) {
	parts := make([]string, len(args))
	for i, a := range args {
		parts[i] = a.String()
	}
	if vm.stdout != nil {
		fmt.Fprintln(vm.stdout, joinStrings(parts, " "))
	}
	return NilValue, nil
}

func joinStrings(parts []string, sep string) string {
	out := ""
	for i, p := range parts {
		if i > 0 {
			out += sep
		}
		out += p
	}
	return out
}

func bLen(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("len takes 1 argument")
	}
	return vm.lengthOf(args[0])
}

func bType(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("type takes 1 argument")
	}
	if args[0].K == KStruct {
		return StringValue(args[0].O.(*StructObj).Type), nil
	}
	return StringValue(args[0].K.String()), nil
}

func bKeys(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 || args[0].K != KMap {
		return NilValue, vm.fail("keys takes a map")
	}
	var elems []Value
	for k := range args[0].O.(*MapObj).Fields {
		elems = append(elems, StringValue(k))
	}
	return Value{K: KArray, O: &ArrayObj{Elems: elems}}, nil
}

func bPush(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 || args[0].K != KArray {
		return NilValue, vm.fail("push takes (array, value)")
	}
	a := args[0].O.(*ArrayObj)
	a.Elems = append(a.Elems, args[1])
	return args[0], nil
}

func bPop(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 || args[0].K != KArray {
		return NilValue, vm.fail("pop takes an array")
	}
	a := args[0].O.(*ArrayObj)
	if len(a.Elems) == 0 {
		return NilValue, vm.fail("pop from empty array")
	}
	v := a.Elems[len(a.Elems)-1]
	a.Elems = a.Elems[:len(a.Elems)-1]
	return v, nil
}

func bRange(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("range takes 1 argument")
	}
	nf, ok := args[0].AsFloat()
	if !ok || nf < 0 || nf != math.Trunc(nf) {
		return NilValue, vm.fail("range needs a non-negative int")
	}
	elems := make([]Value, int(nf))
	for i := range elems {
		elems[i] = IntValue(int64(i))
	}
	return Value{K: KArray, O: &ArrayObj{Elems: elems}}, nil
}

func bStr(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("str takes 1 argument")
	}
	return StringValue(args[0].String()), nil
}

func bInt(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("int takes 1 argument")
	}
	switch args[0].K {
	case KInt:
		return args[0], nil
	case KFloat:
		return IntValue(int64(args[0].F)), nil
	case KBool:
		if args[0].B {
			return IntValue(1), nil
		}
		return IntValue(0), nil
	case KString:
		var i int64
		if _, err := fmt.Sscanf(args[0].S, "%d", &i); err != nil {
			return NilValue, vm.fail("cannot convert %q to int", args[0].S)
		}
		return IntValue(i), nil
	}
	return NilValue, vm.fail("cannot convert %s to int", args[0].K)
}

func bFloat(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("float takes 1 argument")
	}
	if f, ok := args[0].AsFloat(); ok {
		return FloatValue(f), nil
	}
	if args[0].K == KString {
		var f float64
		if _, err := fmt.Sscanf(args[0].S, "%g", &f); err != nil {
			return NilValue, vm.fail("cannot convert %q to float", args[0].S)
		}
		return FloatValue(f), nil
	}
	return NilValue, vm.fail("cannot convert %s to float", args[0].K)
}

func bAssert(vm *VM, args []Value) (Value, error) {
	if len(args) < 1 || !args[0].Truthy() {
		msg := "assertion failed"
		if len(args) > 1 {
			msg = args[1].String()
		}
		return NilValue, vm.fail("%s", msg)
	}
	return NilValue, nil
}

func math1(name string, fn func(float64) float64) func(*VM, []Value) (Value, error) {
	return func(vm *VM, args []Value) (Value, error) {
		if len(args) != 1 {
			return NilValue, vm.fail("%s takes 1 argument", name)
		}
		f, ok := args[0].AsFloat()
		if !ok {
			return NilValue, vm.fail("%s needs a number", name)
		}
		return FloatValue(fn(f)), nil
	}
}

func bAbs(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("abs takes 1 argument")
	}
	switch args[0].K {
	case KInt:
		if args[0].I < 0 {
			return IntValue(-args[0].I), nil
		}
		return args[0], nil
	case KFloat:
		return FloatValue(math.Abs(args[0].F)), nil
	}
	return NilValue, vm.fail("abs needs a number")
}

func bMin(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("min takes 2 arguments")
	}
	a, aok := args[0].AsFloat()
	b, bok := args[1].AsFloat()
	if !aok || !bok {
		return NilValue, vm.fail("min needs numbers")
	}
	if args[0].K == KInt && args[1].K == KInt {
		if args[0].I < args[1].I {
			return args[0], nil
		}
		return args[1], nil
	}
	return FloatValue(math.Min(a, b)), nil
}

func bMax(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("max takes 2 arguments")
	}
	a, aok := args[0].AsFloat()
	b, bok := args[1].AsFloat()
	if !aok || !bok {
		return NilValue, vm.fail("max needs numbers")
	}
	if args[0].K == KInt && args[1].K == KInt {
		if args[0].I > args[1].I {
			return args[0], nil
		}
		return args[1], nil
	}
	return FloatValue(math.Max(a, b)), nil
}

func bPow(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("pow takes 2 arguments")
	}
	a, aok := args[0].AsFloat()
	b, bok := args[1].AsFloat()
	if !aok || !bok {
		return NilValue, vm.fail("pow needs numbers")
	}
	return FloatValue(math.Pow(a, b)), nil
}

func numArg(v Value) (float64, error) {
	f, ok := v.AsFloat()
	if !ok {
		return 0, errNotNum
	}
	return f, nil
}

var errNotNum = errString("need a number")

type errString string

func (e errString) Error() string { return string(e) }

func bVec2(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("vec2 takes 2 arguments")
	}
	x, err := numArg(args[0])
	if err != nil {
		return NilValue, vm.fail("vec2 %s", err)
	}
	y, err := numArg(args[1])
	if err != nil {
		return NilValue, vm.fail("vec2 %s", err)
	}
	return Value{K: KVec2, V: [4]float64{x, y}}, nil
}

func bVec3(vm *VM, args []Value) (Value, error) {
	if len(args) != 3 {
		return NilValue, vm.fail("vec3 takes 3 arguments")
	}
	var o [4]float64
	for i, a := range args {
		f, err := numArg(a)
		if err != nil {
			return NilValue, vm.fail("vec3 %s", err)
		}
		o[i] = f
	}
	return Value{K: KVec3, V: o}, nil
}

func bVec4(vm *VM, args []Value) (Value, error) {
	if len(args) != 4 {
		return NilValue, vm.fail("vec4 takes 4 arguments")
	}
	var o [4]float64
	for i, a := range args {
		f, err := numArg(a)
		if err != nil {
			return NilValue, vm.fail("vec4 %s", err)
		}
		o[i] = f
	}
	return Value{K: KVec4, V: o}, nil
}

func bQuat(vm *VM, args []Value) (Value, error) {
	if len(args) != 4 {
		return NilValue, vm.fail("quat takes (x, y, z, w)")
	}
	var o [4]float64
	for i, a := range args {
		f, err := numArg(a)
		if err != nil {
			return NilValue, vm.fail("quat %s", err)
		}
		o[i] = f
	}
	return Value{K: KQuat, V: o}, nil
}

func bMat4Identity(vm *VM, args []Value) (Value, error) {
	return Value{K: KMat4, O: IdentityMat4()}, nil
}

func vec3Arg(v Value) (Vec3, error) {
	switch v.K {
	case KVec3:
		return Vec3{v.V[0], v.V[1], v.V[2]}, nil
	case KVec2:
		return Vec3{v.V[0], v.V[1], 0}, nil
	case KStruct:
		s := v.O.(*StructObj)
		get := func(k string) float64 {
			if f, ok := s.Fields[k]; ok {
				n, _ := f.AsFloat()
				return n
			}
			return 0
		}
		return Vec3{get("x"), get("y"), get("z")}, nil
	}
	return Vec3{}, errNotNum
}

func matArg(v Value) (Mat4, error) {
	if v.K == KMat4 {
		return v.O.(Mat4), nil
	}
	return Mat4{}, errString("need a mat4")
}

func bMat4Translate(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("mat4_translate takes (mat, vec3)")
	}
	m, err := matArg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	t, err := vec3Arg(args[1])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	return Value{K: KMat4, O: m.Translate(t)}, nil
}

func bMat4Scale(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("mat4_scale takes (mat, vec3)")
	}
	m, err := matArg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	t, err := vec3Arg(args[1])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	return Value{K: KMat4, O: m.Scale(t)}, nil
}

func bMat4Rotate(vm *VM, args []Value) (Value, error) {
	if len(args) != 3 {
		return NilValue, vm.fail("mat4_rotate takes (mat, angle, axis)")
	}
	m, err := matArg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	ang, err := numArg(args[1])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	ax, err := vec3Arg(args[2])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	return Value{K: KMat4, O: m.Rotate(ang, ax)}, nil
}

func bMat4Perspective(vm *VM, args []Value) (Value, error) {
	if len(args) != 4 {
		return NilValue, vm.fail("mat4_perspective takes (fovy, aspect, near, far)")
	}
	var q [4]float64
	for i, a := range args {
		f, err := numArg(a)
		if err != nil {
			return NilValue, vm.fail("%s", err)
		}
		q[i] = f
	}
	return Value{K: KMat4, O: Perspective(q[0], q[1], q[2], q[3])}, nil
}

func bMat4LookAt(vm *VM, args []Value) (Value, error) {
	if len(args) != 3 {
		return NilValue, vm.fail("mat4_lookat takes (eye, center, up)")
	}
	e, err := vec3Arg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	c, err := vec3Arg(args[1])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	u, err := vec3Arg(args[2])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	return Value{K: KMat4, O: LookAt(e, c, u)}, nil
}

func bDot(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("dot takes 2 arguments")
	}
	a, err := vec3Arg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	b, err := vec3Arg(args[1])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	return FloatValue(a.Dot(b)), nil
}

func bCross(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("cross takes 2 arguments")
	}
	a, err := vec3Arg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	b, err := vec3Arg(args[1])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	r := a.Cross(b)
	return Value{K: KVec3, V: [4]float64{r.X, r.Y, r.Z}}, nil
}

func bNormalize(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("normalize takes 1 argument")
	}
	a, err := vec3Arg(args[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	r := a.Norm()
	return Value{K: KVec3, V: [4]float64{r.X, r.Y, r.Z}}, nil
}

func bLerp(vm *VM, args []Value) (Value, error) {
	if len(args) != 3 {
		return NilValue, vm.fail("lerp takes 3 arguments")
	}
	a, aok := args[0].AsFloat()
	b, bok := args[1].AsFloat()
	t, tok := args[2].AsFloat()
	if !aok || !bok || !tok {
		return NilValue, vm.fail("lerp needs numbers")
	}
	return FloatValue(Lerp(a, b, t)), nil
}

// ---------------------------------------------------------------------------
// World builtins
// ---------------------------------------------------------------------------

// worldArg resolves the target world: explicit first argument or default.
func worldArg(vm *VM, args []Value) (*ecs.World, []Value, error) {
	if len(args) > 0 && args[0].K == KWorld {
		if w, ok := args[0].O.(*ecs.World); ok {
			return w, args[1:], nil
		}
	}
	if vm.world != nil {
		return vm.world, args, nil
	}
	return nil, nil, vm.fail("no world: pass one or SetWorld first")
}

func entityArg(v Value) (ecs.EntityID, error) {
	if v.K != KEntity {
		return ecs.EntityID{}, errString("need an entity")
	}
	r := v.O.(EntityRef)
	return ecs.EntityID{Index: r.Index, Gen: r.Gen}, nil
}

// bSpawn creates an entity: spawn(world?, "Prefab") or spawn(world?, struct).
func bSpawn(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) == 0 {
		id := w.Spawn(nil)
		return entityValue(id), nil
	}
	switch rest[0].K {
	case KString:
		return vm.spawnPrefab(w, rest[0].S)
	case KStruct:
		s := rest[0].O.(*StructObj)
		if _, isEntity := vm.moduleEntities()[s.Type]; isEntity {
			return vm.spawnPrefabValue(w, rest[0])
		}
		comps := map[string]map[string]any{}
		for k, v := range s.Fields {
			if sub, ok := v.O.(*StructObj); ok {
				comps[k] = flatten(sub.Fields)
			} else {
				comps[k] = map[string]any{"value": toAny(v)}
			}
		}
		id := w.Spawn(comps)
		vm.firePrefabObservers(w, id, comps)
		return entityValue(id), nil
	}
	return NilValue, vm.fail("spawn needs a prefab name or struct")
}

func entityValue(id ecs.EntityID) Value {
	return Value{K: KEntity, O: EntityRef{Index: id.Index, Gen: id.Gen}}
}

// moduleEntities is overridden by prefab compilation (see compiler_ecs.go);
// the default reports no prefabs.
func (vm *VM) moduleEntities() map[string]bool { return vm.prefabs }

func (vm *VM) firePrefabObservers(w *ecs.World, id ecs.EntityID, comps map[string]map[string]any) {
	for cn, fields := range comps {
		snap := Value{K: KStruct, O: &StructObj{Type: cn, Fields: snapshot(fields)}}
		_ = id
		_ = vm.fireObservers(0, cn, snap, vm.budget)
	}
}

func bDespawn(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 1 {
		return NilValue, vm.fail("despawn takes an entity")
	}
	id, err := entityArg(rest[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	for _, cn := range w.ComponentsOf(id) {
		f, _ := w.Get(id, cn)
		_ = vm.fireObservers(1, cn, Value{K: KStruct, O: &StructObj{Type: cn, Fields: snapshot(f)}}, vm.budget)
	}
	w.Despawn(id)
	return NilValue, nil
}

func bAddComponent(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 3 {
		return NilValue, vm.fail("add_component takes (entity, name, struct)")
	}
	id, err := entityArg(rest[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	if rest[1].K != KString || rest[2].K != KStruct {
		return NilValue, vm.fail("add_component takes (entity, name, struct)")
	}
	s := rest[2].O.(*StructObj)
	w.Add(id, rest[1].S, flatten(s.Fields))
	_ = vm.fireObservers(0, rest[1].S, rest[2], vm.budget)
	return NilValue, nil
}

func bRemoveComponent(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 2 {
		return NilValue, vm.fail("remove_component takes (entity, name)")
	}
	id, err := entityArg(rest[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	if rest[1].K != KString {
		return NilValue, vm.fail("remove_component takes (entity, name)")
	}
	f, _ := w.Get(id, rest[1].S)
	w.Remove(id, rest[1].S)
	_ = vm.fireObservers(1, rest[1].S, Value{K: KStruct, O: &StructObj{Type: rest[1].S, Fields: snapshot(f)}}, vm.budget)
	return NilValue, nil
}

func bHasComponent(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 2 {
		return NilValue, vm.fail("has_component takes (entity, name)")
	}
	id, err := entityArg(rest[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	if rest[1].K != KString {
		return NilValue, vm.fail("has_component takes (entity, name)")
	}
	return BoolValue(w.Has(id, rest[1].S)), nil
}

func bGetComponent(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 2 {
		return NilValue, vm.fail("get_component takes (entity, name)")
	}
	id, err := entityArg(rest[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	if rest[1].K != KString {
		return NilValue, vm.fail("get_component takes (entity, name)")
	}
	f, ok := w.Get(id, rest[1].S)
	if !ok {
		return NilValue, nil
	}
	return Value{K: KStruct, O: &StructObj{Type: rest[1].S, Fields: snapshot(f)}}, nil
}

func bSetResource(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 2 || rest[0].K != KString {
		return NilValue, vm.fail("set_resource takes (name, value)")
	}
	w.SetResource(rest[0].S, toAny(rest[1]))
	return NilValue, nil
}

func bGetResource(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 1 || rest[0].K != KString {
		return NilValue, vm.fail("get_resource takes (name)")
	}
	a, ok := w.GetResource(rest[0].S)
	if !ok {
		return NilValue, nil
	}
	return fromAny(a), nil
}

// bSetParent links a child entity under a parent: set_parent(world?,
// child, parent). Struct children spawn first.
func bSetParent(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 2 {
		return NilValue, vm.fail("set_parent takes (child, parent)")
	}
	child := rest[0]
	if child.K == KStruct {
		child, err = vm.spawnPrefabValue(w, child)
		if err != nil {
			return NilValue, err
		}
	}
	if err := vm.linkParent(w, child, rest[1]); err != nil {
		return NilValue, vm.fail("%s", err)
	}
	return child, nil
}

func bGetParent(vm *VM, args []Value) (Value, error) {
	w, rest, err := worldArg(vm, args)
	if err != nil {
		return NilValue, err
	}
	if len(rest) != 1 {
		return NilValue, vm.fail("get_parent takes (child)")
	}
	id, err := entityArg(rest[0])
	if err != nil {
		return NilValue, vm.fail("%s", err)
	}
	p, ok := w.GetParent(id)
	if !ok {
		return NilValue, nil
	}
	return entityValue(p), nil
}
