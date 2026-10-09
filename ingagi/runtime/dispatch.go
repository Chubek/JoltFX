package runtime

import (
	"math"
	"strings"

	"github.com/Chubek/JoltFX/ingagi/ecs"
)

// Dispatch: the execution loop lives here (run), driven by the direct-
// threaded table in direct_threaded_code.go. Helpers implement value
// semantics: arithmetic with promotion, structural equality, field/index
// access, calls, and the ECS bridges (foreach, emit, observe, bind).

// run executes from pc until Halt/Return-out/Halt. The result is left for
// callEntry. Instruction counting is VM-wide so budgets span nested calls.
func (vm *VM) run(pc int, b Budget) (Value, error) {
	if vm.tape == nil {
		return NilValue, runtimeErrorf("no module loaded")
	}
	code := vm.tape.Code
	for {
		if pc < 0 || pc >= len(code) {
			return NilValue, vm.fail("pc out of range %d", pc)
		}
		if b.MaxInstrs > 0 {
			vm.execCount++
			if vm.execCount > b.MaxInstrs {
				return NilValue, vm.fail("instruction budget exceeded (%d)", b.MaxInstrs)
			}
		}
		fr := vm.frames.top()
		if fr == nil {
			return NilValue, vm.fail("empty call stack")
		}
		npc, rerr := threadedTable[vm.tape.Code[pc].Op](vm, fr, vm.tape.Code[pc], pc)
		if rerr != nil {
			if len(rerr.Trace) == 0 {
				rerr.Trace = vm.trace()
			}
			return NilValue, rerr
		}
		if npc < 0 {
			return vm.stack.pop(), nil
		}
		pc = npc
	}
}

// callEntry invokes a tape entry with args. Missing args fill nil up to the
// declared arity; extras are an error.
func (vm *VM) callEntry(pc int, name string, up []Value, args []Value, b Budget) (Value, error) {
	depth, stackBase, iterBase := vm.frames.depth(), vm.stack.depth(), len(vm.iters)
	if depth == 0 {
		vm.execCount = 0
		oldBudget, oldLimit := vm.activeBudget, vm.frames.limit
		vm.activeBudget = b
		if b.MaxFrames > 0 {
			vm.frames.limit = b.MaxFrames
		}
		defer func() { vm.activeBudget, vm.frames.limit = oldBudget, oldLimit }()
	} else {
		// Nested calls, hooks and host re-entry share the outer invocation's
		// counter and limits, even when invoked through a default-budget API.
		b = vm.activeBudget
	}
	defer func() {
		// Errors must not poison subsequent calls. World/global mutations are
		// not transactional; unfinished iterator snapshots are discarded.
		clear(vm.frames.frames[depth:])
		vm.frames.frames = vm.frames.frames[:depth]
		vm.stack.reset(stackBase)
		clear(vm.iters[iterBase:])
		vm.iters = vm.iters[:iterBase]
	}()
	if want, ok := vm.params[name]; ok {
		if len(args) < want {
			for len(args) < want {
				args = append(args, NilValue)
			}
		} else if len(args) > want {
			return NilValue, runtimeErrorf("arity: %s takes %d args, got %d", name, want, len(args))
		}
	}
	fr := frame{name: name, ret: -1, base: stackBase, up: up, iterBase: iterBase}
	fr.locals = append(fr.locals, args...)
	if err := vm.frames.push(fr); err != nil {
		return NilValue, &RuntimeError{Msg: err.Error(), Trace: vm.trace()}
	}
	return vm.run(pc, b)
}

// ensure grows frame locals to cover slot.
func (fr *frame) ensure(slot int) {
	for len(fr.locals) <= slot {
		fr.locals = append(fr.locals, NilValue)
	}
}

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------

func (vm *VM) resolveGlobal(name string) (Value, bool) {
	if v, ok := vm.globals[name]; ok {
		return v, true
	}
	if pc, ok := vm.funcs[name]; ok {
		_ = pc
		return Value{K: KClosure, O: &ClosureObj{Name: name}}, true
	}
	if b, ok := vm.builtins[name]; ok {
		return Value{K: KBuiltin, O: &BuiltinObj{Name: name, Fn: b}}, true
	}
	if h, ok := vm.hostFns[name]; ok {
		return Value{K: KBuiltin, O: &BuiltinObj{Name: name, Fn: h}}, true
	}
	// Unique `Type::name` suffix fallback (bare enum variants, short paths).
	var cand Value
	n := 0
	for k, v := range vm.globals {
		if strings.HasSuffix(k, "::"+name) {
			cand, n = v, n+1
		}
	}
	if n == 1 {
		return cand, true
	}
	return NilValue, false
}

// ---------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------

// callValue invokes a callee value (closure/builtin) with args.
func (vm *VM) callValue(callee Value, args []Value, b Budget) (Value, error) {
	switch c := callee.O.(type) {
	case *ClosureObj:
		pc, ok := vm.funcs[c.Name]
		if !ok {
			return NilValue, vm.fail("undefined function %q", c.Name)
		}
		return vm.callEntry(pc, c.Name, c.Up, args, b)
	case *BuiltinObj:
		return c.Fn(vm, args)
	}
	return NilValue, vm.fail("not callable: %s", callee.K)
}

// callNamed resolves a static name to callee and invokes it.
func (vm *VM) callNamed(name string, args []Value, b Budget) (Value, error) {
	if pc, ok := vm.funcs[name]; ok {
		return vm.callEntry(pc, name, nil, args, b)
	}
	if fn, ok := vm.builtins[name]; ok {
		return fn(vm, args)
	}
	if fn, ok := vm.hostFns[name]; ok {
		return fn(vm, args)
	}
	// Enum payload constructor `Type::Variant(args)`.
	if tn, vn, ok := splitVariant(name); ok {
		if ti, ok := vm.types[tn]; ok && ti.Kind == "enum" {
			for _, v := range ti.Variants {
				if v == vn {
					return Value{K: KEnum, O: &EnumObj{Type: tn, Variant: vn, Payload: args}}, nil
				}
			}
		}
	}
	// Unique suffix fallback.
	var cand Value
	n := 0
	for k := range vm.funcs {
		if strings.HasSuffix(k, "::"+name) || k == name {
			if pc, ok := vm.funcs[k]; ok {
				_ = pc
				cand = Value{K: KClosure, O: &ClosureObj{Name: k}}
				n++
			}
		}
	}
	if n == 1 {
		return vm.callValue(cand, args, b)
	}
	return NilValue, vm.fail("undefined function %q", name)
}

func splitVariant(name string) (string, string, bool) {
	i := strings.LastIndex(name, "::")
	if i < 0 {
		return "", "", false
	}
	return name[:i], name[i+2:], true
}

// callMethod invokes obj.name(args) with receiver as arg0. The pc feeds
// the monomorphic method cache (see jit.go).
func (vm *VM) callMethod(obj Value, name string, args []Value, b Budget, pc int) (Value, error) {
	tn := receiverType(obj)
	if entry, ok := vm.methodCache(pc, tn); ok {
		if epc, ok := vm.funcs[entry]; ok {
			return vm.callEntry(epc, entry, nil, append([]Value{obj}, args...), b)
		}
	}
	if entry, ok := vm.methods[tn+"::"+name]; ok {
		if epc, ok := vm.funcs[entry]; ok {
			vm.methodCacheFill(pc, tn, name, entry)
			return vm.callEntry(epc, entry, nil, append([]Value{obj}, args...), b)
		}
	}
	if fn, ok := vm.hostFns[tn+"::"+name]; ok {
		return fn(vm, append([]Value{obj}, args...))
	}
	// Generic builtins accept the receiver as first argument, giving
	// every value `.len()`, `.push(x)`, `.keys()` and friends for free.
	if fn, ok := vm.builtins[name]; ok {
		return fn(vm, append([]Value{obj}, args...))
	}
	return NilValue, vm.fail("no method %q on %s", name, obj.K)
}

// receiverType names a value's method namespace.
func receiverType(v Value) string {
	switch v.K {
	case KStruct:
		return v.O.(*StructObj).Type
	case KEnum:
		return v.O.(*EnumObj).Type
	case KArray:
		return "array"
	case KMap:
		return "map"
	case KString:
		return "string"
	case KEntity:
		return "entity"
	case KWorld:
		return "world"
	case KVec2:
		return "vec2"
	case KVec3:
		return "vec3"
	case KVec4:
		return "vec4"
	case KMat4:
		return "mat4"
	case KQuat:
		return "quat"
	}
	return v.K.String()
}

// ---------------------------------------------------------------------------
// Arithmetic with promotion (int/float/string/vec/mat/quat).
// ---------------------------------------------------------------------------

func numPair(a, b Value) (float64, float64, bool) {
	af, aok := a.AsFloat()
	bf, bok := b.AsFloat()
	return af, bf, aok && bok
}

func (vm *VM) arith(op Op, a, b Value, pc int) (Value, error) {
	// Integer fast path (also feeds the specializing profiler).
	if a.K == KInt && b.K == KInt {
		vm.noteKind(pc, 1)
		switch op {
		case OpAdd:
			return IntValue(a.I + b.I), nil
		case OpSub:
			return IntValue(a.I - b.I), nil
		case OpMul:
			return IntValue(a.I * b.I), nil
		case OpDiv:
			if b.I == 0 {
				return NilValue, vm.fail("integer division by zero")
			}
			return IntValue(a.I / b.I), nil
		case OpMod:
			if b.I == 0 {
				return NilValue, vm.fail("integer modulo by zero")
			}
			return IntValue(a.I % b.I), nil
		}
		return NilValue, vm.fail("bad int operator")
	}
	if af, bf, ok := numPair(a, b); ok {
		vm.noteKind(pc, 2)
		switch op {
		case OpAdd:
			return FloatValue(af + bf), nil
		case OpSub:
			return FloatValue(af - bf), nil
		case OpMul:
			return FloatValue(af * bf), nil
		case OpDiv:
			return FloatValue(af / bf), nil
		case OpMod:
			return FloatValue(math.Mod(af, bf)), nil
		}
	}
	if op == OpAdd {
		if a.K == KString && b.K == KString {
			return StringValue(a.S + b.S), nil
		}
		if r, ok := vecArith(op, a, b); ok {
			return r, nil
		}
		if r, ok := matArith(op, a, b); ok {
			return r, nil
		}
		if a.K == KQuat && b.K == KQuat {
			return Value{K: KQuat, V: quatToArr(a.O.(Quat).Mul(b.O.(Quat)))}, nil
		}
		if a.K == KQuat && b.K == KVec3 {
			q := a.O.(Quat)
			r := q.RotateVec(Vec3{b.V[0], b.V[1], b.V[2]})
			return Value{K: KVec3, V: [4]float64{r.X, r.Y, r.Z}}, nil
		}
	}
	if op == OpSub || op == OpMul || op == OpDiv {
		if r, ok := vecArith(op, a, b); ok {
			return r, nil
		}
		if r, ok := matArith(op, a, b); ok {
			return r, nil
		}
	}
	return NilValue, vm.fail("cannot %s %s and %s", OpName(op), a.K, b.K)
}

func quatToArr(q Quat) [4]float64 { return [4]float64{q.X, q.Y, q.Z, q.W} }

// vecArith handles vec+vec, vec*scalar, scalar*vec for vec2/3/4.
func vecArith(op Op, a, b Value) (Value, bool) {
	ak := a.K == KVec2 || a.K == KVec3 || a.K == KVec4
	bk := b.K == KVec2 || b.K == KVec3 || b.K == KVec4
	n := 0
	switch {
	case a.K == KVec2 || b.K == KVec2:
		n = 2
	case a.K == KVec3 || b.K == KVec3:
		n = 3
	default:
		n = 4
	}
	get := func(v Value) ([4]float64, bool) {
		if v.K == KVec2 || v.K == KVec3 || v.K == KVec4 {
			return v.V, true
		}
		if f, ok := v.AsFloat(); ok {
			return [4]float64{f, f, f, f}, true
		}
		return [4]float64{}, false
	}
	av, aok := get(a)
	bv, bok := get(b)
	if !aok || !bok || (!ak && !bk) {
		return NilValue, false
	}
	var o [4]float64
	switch op {
	case OpAdd:
		for i := 0; i < n; i++ {
			o[i] = av[i] + bv[i]
		}
	case OpSub:
		for i := 0; i < n; i++ {
			o[i] = av[i] - bv[i]
		}
	case OpMul:
		for i := 0; i < n; i++ {
			o[i] = av[i] * bv[i]
		}
	case OpDiv:
		for i := 0; i < n; i++ {
			o[i] = av[i] / bv[i]
		}
	default:
		return NilValue, false
	}
	k := KVec4
	if n == 2 {
		k = KVec2
	} else if n == 3 {
		k = KVec3
	}
	return Value{K: k, V: o}, true
}

// matArith handles mat*mat and mat*vec4.
func matArith(op Op, a, b Value) (Value, bool) {
	if op != OpMul && op != OpAdd && op != OpSub {
		return NilValue, false
	}
	if a.K == KMat4 && b.K == KMat4 && op == OpMul {
		r := a.O.(Mat4).Mul(b.O.(Mat4))
		return Value{K: KMat4, O: r}, true
	}
	if a.K == KMat4 && b.K == KVec4 && op == OpMul {
		r := a.O.(Mat4).MulVec4(Vec4{b.V[0], b.V[1], b.V[2], b.V[3]})
		return Value{K: KVec4, V: [4]float64{r.X, r.Y, r.Z, r.W}}, true
	}
	return NilValue, false
}

// valueEqual is structural equality across kinds (numbers cross int/float).
func valueEqual(a, b Value) bool {
	if a.K != b.K {
		if af, ok := a.AsFloat(); ok {
			if bf, ok := b.AsFloat(); ok {
				return af == bf
			}
		}
		return false
	}
	switch a.K {
	case KNil:
		return true
	case KBool:
		return a.B == b.B
	case KInt:
		return a.I == b.I
	case KFloat:
		return a.F == b.F
	case KString:
		return a.S == b.S
	case KVec2, KVec3, KVec4, KQuat:
		return a.V == b.V
	case KMat4:
		am := a.O.(Mat4)
		bm := b.O.(Mat4)
		return am == bm
	case KArray:
		aa := a.O.(*ArrayObj).Elems
		ba := b.O.(*ArrayObj).Elems
		if len(aa) != len(ba) {
			return false
		}
		for i := range aa {
			if !valueEqual(aa[i], ba[i]) {
				return false
			}
		}
		return true
	case KMap:
		am := a.O.(*MapObj).Fields
		bm := b.O.(*MapObj).Fields
		if len(am) != len(bm) {
			return false
		}
		for k, v := range am {
			w, ok := bm[k]
			if !ok || !valueEqual(v, w) {
				return false
			}
		}
		return true
	case KStruct:
		as := a.O.(*StructObj)
		bs := b.O.(*StructObj)
		if as.Type != bs.Type || len(as.Fields) != len(bs.Fields) {
			return false
		}
		for k, v := range as.Fields {
			w, ok := bs.Fields[k]
			if !ok || !valueEqual(v, w) {
				return false
			}
		}
		return true
	case KEnum:
		ae := a.O.(*EnumObj)
		be := b.O.(*EnumObj)
		if ae.Type != be.Type || ae.Variant != be.Variant || len(ae.Payload) != len(be.Payload) {
			return false
		}
		for i := range ae.Payload {
			if !valueEqual(ae.Payload[i], be.Payload[i]) {
				return false
			}
		}
		return true
	case KEntity:
		return a.O.(EntityRef) == b.O.(EntityRef)
	default:
		return false
	}
}

// compare implements ordered comparison (numbers, strings).
func (vm *VM) compare(op Op, a, b Value) (Value, error) {
	if af, bf, ok := numPair(a, b); ok {
		var r bool
		switch op {
		case OpLt:
			r = af < bf
		case OpLe:
			r = af <= bf
		case OpGt:
			r = af > bf
		case OpGe:
			r = af >= bf
		}
		return BoolValue(r), nil
	}
	if a.K == KString && b.K == KString {
		var r bool
		switch op {
		case OpLt:
			r = a.S < b.S
		case OpLe:
			r = a.S <= b.S
		case OpGt:
			r = a.S > b.S
		case OpGe:
			r = a.S >= b.S
		}
		return BoolValue(r), nil
	}
	return NilValue, vm.fail("cannot compare %s and %s", a.K, b.K)
}

// ---------------------------------------------------------------------------
// Fields and indexing
// ---------------------------------------------------------------------------

var vecFields = map[string]int{
	"x": 0, "y": 1, "z": 2, "w": 3,
	"r": 0, "g": 1, "b": 2, "a": 3,
}

func (vm *VM) loadField(obj Value, name string) (Value, error) {
	switch obj.K {
	case KStruct:
		if v, ok := obj.O.(*StructObj).Fields[name]; ok {
			return v, nil
		}
		return NilValue, vm.fail("no field %q on %s", name, obj.O.(*StructObj).Type)
	case KMap:
		if v, ok := obj.O.(*MapObj).Fields[name]; ok {
			return v, nil
		}
		return NilValue, vm.fail("no key %q", name)
	case KVec2, KVec3, KVec4, KQuat:
		if i, ok := vecFields[name]; ok {
			if obj.K == KVec2 && i > 1 {
				return NilValue, vm.fail("no field %q on vec2", name)
			}
			if obj.K == KVec3 && i > 2 {
				return NilValue, vm.fail("no field %q on vec3", name)
			}
			return FloatValue(obj.V[i]), nil
		}
		return NilValue, vm.fail("no field %q on %s", name, obj.K)
	case KEntity:
		if name == "id" {
			return IntValue(int64(obj.O.(EntityRef).Index)), nil
		}
		return NilValue, vm.fail("no field %q on entity", name)
	}
	return NilValue, vm.fail("cannot access field %q on %s", name, obj.K)
}

// tryField is the total version of field access (missing -> nil).
func (vm *VM) tryField(obj Value, name string) Value {
	v, err := vm.loadField(obj, name)
	if err != nil {
		return NilValue
	}
	return v
}

// storeField sets obj[field] = val and returns the updated object. Value
// types (vec/quat) update by copy and return the new value; the compiler
// rebinds simple roots so `p.x = 1` works on locals too.
func (vm *VM) storeField(obj, val Value, name string) (Value, error) {
	switch obj.K {
	case KStruct:
		obj.O.(*StructObj).Fields[name] = val
		return obj, nil
	case KMap:
		obj.O.(*MapObj).Fields[name] = val
		return obj, nil
	case KVec2, KVec3, KVec4, KQuat:
		i, ok := vecFields[name]
		if !ok {
			return NilValue, vm.fail("no field %q on %s", name, obj.K)
		}
		f, ok := val.AsFloat()
		if !ok {
			return NilValue, vm.fail("cannot store %s in %s.%s", val.K, obj.K, name)
		}
		obj.V[i] = f
		return obj, nil
	}
	return NilValue, vm.fail("cannot store field %q on %s", name, obj.K)
}

func (vm *VM) loadIndex(obj, idx Value) (Value, error) {
	switch obj.K {
	case KArray:
		i, ok := idx.AsFloat()
		if !ok {
			return NilValue, vm.fail("array index must be a number")
		}
		n := int(i)
		elems := obj.O.(*ArrayObj).Elems
		if n < 0 || n >= len(elems) {
			return NilValue, vm.fail("index %d out of range (len %d)", n, len(elems))
		}
		return elems[n], nil
	case KString:
		i, ok := idx.AsFloat()
		if !ok {
			return NilValue, vm.fail("string index must be a number")
		}
		runes := []rune(obj.S)
		n := int(i)
		if n < 0 || n >= len(runes) {
			return NilValue, vm.fail("index %d out of range", n)
		}
		return StringValue(string(runes[n])), nil
	case KMap:
		if idx.K != KString {
			return NilValue, vm.fail("map index must be a string")
		}
		if v, ok := obj.O.(*MapObj).Fields[idx.S]; ok {
			return v, nil
		}
		return NilValue, nil
	}
	return NilValue, vm.fail("cannot index %s", obj.K)
}

func (vm *VM) storeIndex(obj, idx, val Value) error {
	switch obj.K {
	case KArray:
		i, ok := idx.AsFloat()
		if !ok {
			return vm.fail("array index must be a number")
		}
		n := int(i)
		elems := obj.O.(*ArrayObj).Elems
		if n < 0 || n >= len(elems) {
			return vm.fail("index %d out of range (len %d)", n, len(elems))
		}
		elems[n] = val
		return nil
	case KMap:
		if idx.K != KString {
			return vm.fail("map index must be a string")
		}
		obj.O.(*MapObj).Fields[idx.S] = val
		return nil
	}
	return vm.fail("cannot store into %s", obj.K)
}

func (vm *VM) lengthOf(v Value) (Value, error) {
	switch v.K {
	case KArray:
		return IntValue(int64(len(v.O.(*ArrayObj).Elems))), nil
	case KString:
		return IntValue(int64(len([]rune(v.S)))), nil
	case KMap:
		return IntValue(int64(len(v.O.(*MapObj).Fields))), nil
	case KStruct:
		return IntValue(int64(len(v.O.(*StructObj).Fields))), nil
	}
	return NilValue, vm.fail("no length for %s", v.K)
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

// makeStruct builds a struct/component value, filling missing fields from
// load-time defaults.
func (vm *VM) makeStruct(typeName string, pairs []Value) (Value, error) {
	fields := map[string]Value{}
	for i := 0; i+1 < len(pairs); i += 2 {
		if pairs[i].K != KString {
			return NilValue, vm.fail("field name must be a string")
		}
		fields[pairs[i].S] = pairs[i+1]
	}
	for k, v := range vm.defs[typeName] {
		if _, ok := fields[k]; !ok {
			fields[k] = v
		}
	}
	return Value{K: KStruct, O: &StructObj{Type: typeName, Fields: fields}}, nil
}

// ---------------------------------------------------------------------------
// ECS bridges
// ---------------------------------------------------------------------------

// toAny converts a runtime Value for schemaless ecs storage.
func toAny(v Value) any { return any(v) }

// fromAny converts schemaless storage back, coercing host primitives.
func fromAny(a any) Value {
	switch a := a.(type) {
	case Value:
		return a
	case nil:
		return NilValue
	case bool:
		return BoolValue(a)
	case int:
		return IntValue(int64(a))
	case int32:
		return IntValue(int64(a))
	case int64:
		return IntValue(a)
	case uint32:
		return IntValue(int64(a))
	case float32:
		return FloatValue(float64(a))
	case float64:
		return FloatValue(a)
	case string:
		return StringValue(a)
	}
	return Value{K: KHandle, O: a}
}

// snapshot converts component fields to Values.
func snapshot(m map[string]any) map[string]Value {
	out := make(map[string]Value, len(m))
	for k, v := range m {
		out[k] = fromAny(v)
	}
	return out
}

// flatten converts a snapshot back to storage form.
func flatten(m map[string]Value) map[string]any {
	out := make(map[string]any, len(m))
	for k, v := range m {
		out[k] = toAny(v)
	}
	return out
}

// foreachBegin starts iteration over entities carrying all comps.
func (vm *VM) foreachBegin(world *ecs.World, comps []string, muts []bool, aliases []string, slots []int32) error {
	entities := world.Query(comps, nil)
	it := foreachIter{entities: entities, mut: muts, alias: aliases, comp: comps, world: world, idx: -1}
	it.rows = make([][]Value, len(entities))
	for i, e := range entities {
		row := make([]Value, len(comps))
		for j, cn := range comps {
			f, _ := world.Get(e, cn)
			row[j] = Value{K: KStruct, O: &StructObj{Type: cn, Fields: snapshot(f)}}
		}
		it.rows[i] = row
	}
	it.slots = slots
	vm.iters = append(vm.iters, it)
	return nil
}

// foreachNext advances; false exhausts the iterator.
func (vm *VM) foreachNext(fr *frame) (bool, error) {
	if len(vm.iters) == 0 {
		return false, vm.fail("foreach without iterator")
	}
	it := &vm.iters[len(vm.iters)-1]
	vm.flushRow(fr, it)
	it.idx++
	if it.idx >= len(it.entities) {
		return false, nil
	}
	for j, v := range it.rows[it.idx] {
		fr.ensure(int(it.slots[j]))
		fr.locals[it.slots[j]] = v
	}
	return true, nil
}

// flushRow writes back &mut snapshots of the current row.
func (vm *VM) flushRow(fr *frame, it *foreachIter) {
	if it.idx < 0 || it.idx >= len(it.entities) {
		return
	}
	e := it.entities[it.idx]
	for j, isMut := range it.mut {
		if !isMut {
			continue
		}
		fr.ensure(int(it.slots[j]))
		if s, ok := fr.locals[it.slots[j]].O.(*StructObj); ok {
			it.world.Set(e, it.comp[j], flatten(s.Fields))
			it.rows[it.idx][j] = fr.locals[it.slots[j]]
		}
	}
}

// foreachEnd finishes iteration, flushing the final row.
func (vm *VM) foreachEnd(fr *frame) {
	if len(vm.iters) == 0 {
		return
	}
	it := &vm.iters[len(vm.iters)-1]
	vm.flushRow(fr, it)
	vm.iters = vm.iters[:len(vm.iters)-1]
}

// fireObservers runs inline `on` hooks for a component mutation.
func (vm *VM) fireObservers(kind int, comp string, snap Value, b Budget) error {
	for _, o := range vm.observers {
		if o.kind != kind || o.comp != comp {
			continue
		}
		if _, err := vm.callEntry(o.entry, "<observer>", o.up, []Value{snap}, b); err != nil {
			return err
		}
	}
	return nil
}

// emit publishes an event struct and runs on_event hooks synchronously.
func (vm *VM) emit(ev Value, b Budget) error {
	s, ok := ev.O.(*StructObj)
	if !ok {
		return vm.fail("emit requires an event struct, got %s", ev.K)
	}
	vm.events = append(vm.events, Event{Type: s.Type, Fields: s.Fields})
	if vm.emitDepth > 64 {
		return vm.fail("event recursion too deep")
	}
	vm.emitDepth++
	defer func() { vm.emitDepth-- }()
	for _, pc := range vm.evHooks[s.Type] {
		if _, err := vm.callEntry(pc, s.Type+"::on_event", nil, []Value{ev}, b); err != nil {
			return err
		}
	}
	return nil
}

// noteKind records operand kinds for the specializing tier (see jit.go).
func (vm *VM) noteKind(pc int, kind int) {
	pr, ok := vm.profiles[pc]
	if !ok {
		pr = &opProfile{}
		vm.profiles[pc] = pr
	}
	pr.count++
	pr.seen |= kind
}
