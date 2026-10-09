package runtime

import "github.com/Chubek/JoltFX/ingagi/ecs"

// Direct-threaded dispatch: every opcode maps to one handler function and
// the run loop dispatches through the table with no switch. The table also
// powers tooling: HandlerName/HasHandler enumerate coverage, and HotReport
// ranks profiled call sites for the specializing tier (see jit.go).

// handler executes one instruction; npc < 0 finishes the current run()
// with the result on the stack top.
type handler func(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError)

var threadedTable [256]handler

func init() {
	threadedTable[OpPushNil] = hPushNil
	threadedTable[OpPushBool] = hPushBool
	threadedTable[OpPushInt] = hPushInt
	threadedTable[OpPushFloat] = hPushFloat
	threadedTable[OpPushString] = hPushString
	threadedTable[OpLoadLocal] = hLoadLocal
	threadedTable[OpStoreLocal] = hStoreLocal
	threadedTable[OpLoadUp] = hLoadUp
	threadedTable[OpStoreUp] = hStoreUp
	threadedTable[OpLoadGlobal] = hLoadGlobal
	threadedTable[OpStoreGlobal] = hStoreGlobal
	threadedTable[OpLoadField] = hLoadField
	threadedTable[OpStoreField] = hStoreField
	threadedTable[OpTryField] = hTryField
	threadedTable[OpLoadIndex] = hLoadIndex
	threadedTable[OpStoreIndex] = hStoreIndex
	threadedTable[OpDup] = hDup
	threadedTable[OpPop] = hPop
	threadedTable[OpAdd] = hAdd
	threadedTable[OpSub] = hSub
	threadedTable[OpMul] = hMul
	threadedTable[OpDiv] = hDiv
	threadedTable[OpMod] = hMod
	threadedTable[OpNeg] = hNeg
	threadedTable[OpNot] = hNot
	threadedTable[OpEq] = hEq
	threadedTable[OpNe] = hNe
	threadedTable[OpLt] = hLt
	threadedTable[OpLe] = hLe
	threadedTable[OpGt] = hGt
	threadedTable[OpGe] = hGe
	threadedTable[OpBitAnd] = hBitAnd
	threadedTable[OpBitOr] = hBitOr
	threadedTable[OpBitXor] = hBitXor
	threadedTable[OpShl] = hShl
	threadedTable[OpShr] = hShr
	threadedTable[OpJump] = hJump
	threadedTable[OpJumpIfFalse] = hJumpIfFalse
	threadedTable[OpJumpIfTrue] = hJumpIfTrue
	threadedTable[OpJumpIfNil] = hJumpIfNil
	threadedTable[OpReturn] = hReturn
	threadedTable[OpEndDefer] = hEndDefer
	threadedTable[OpCall] = hCall
	threadedTable[OpCallMethod] = hCallMethod
	threadedTable[OpMakeArray] = hMakeArray
	threadedTable[OpMakeArrayRepeat] = hMakeArrayRepeat
	threadedTable[OpMakeMap] = hMakeMap
	threadedTable[OpMakeStruct] = hMakeStruct
	threadedTable[OpMakeEnum] = hMakeEnum
	threadedTable[OpLen] = hLen
	threadedTable[OpForEachBegin] = hForEachBegin
	threadedTable[OpForEachNext] = hForEachNext
	threadedTable[OpForEachEnd] = hForEachEnd
	threadedTable[OpEmit] = hEmit
	threadedTable[OpBind] = hBind
	threadedTable[OpObserve] = hObserve
	threadedTable[OpMakeClosure] = hMakeClosure
	threadedTable[OpDefer] = hDefer
	threadedTable[OpAssert] = hAssert
	threadedTable[OpPrint] = hPrint
	threadedTable[OpHalt] = hHalt
}

// HandlerName reports the handler for an opcode ("" when missing).
func HandlerName(op Op) string {
	if threadedTable[op] == nil {
		return ""
	}
	return OpName(op)
}

// HasHandler reports whether every defined opcode has a handler.
func HasHandler() []Op {
	var missing []Op
	for op := OpPushNil; op <= OpHalt; op++ {
		if threadedTable[op] == nil {
			missing = append(missing, op)
		}
	}
	return missing
}

func hPushNil(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(NilValue)
	return pc + 1, nil
}

func hPushBool(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(BoolValue(in.A != 0))
	return pc + 1, nil
}

func hPushInt(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(IntValue(vm.tape.Pool.Int(int(in.C))))
	return pc + 1, nil
}

func hPushFloat(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(FloatValue(vm.tape.Pool.Float(int(in.C))))
	return pc + 1, nil
}

func hPushString(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(StringValue(vm.tape.Pool.String(int(in.C))))
	return pc + 1, nil
}

func hLoadLocal(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	fr.ensure(int(in.A))
	vm.stack.push(fr.locals[in.A])
	return pc + 1, nil
}

func hStoreLocal(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	fr.ensure(int(in.A))
	fr.locals[in.A] = vm.stack.pop()
	return pc + 1, nil
}

func hLoadUp(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if int(in.A) >= len(fr.up) {
		return pc, vm.fail("upvalue %d out of range", in.A)
	}
	vm.stack.push(fr.up[in.A])
	return pc + 1, nil
}

func hStoreUp(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if int(in.A) >= len(fr.up) {
		return pc, vm.fail("upvalue %d out of range", in.A)
	}
	fr.up[in.A] = vm.stack.pop()
	return pc + 1, nil
}

func hLoadGlobal(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	name := vm.tape.Pool.String(int(in.C))
	v, ok := vm.resolveGlobal(name)
	if !ok {
		return pc, vm.fail("undefined name %q", name)
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hStoreGlobal(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.globals[vm.tape.Pool.String(int(in.C))] = vm.stack.pop()
	return pc + 1, nil
}

func hLoadField(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	obj := vm.stack.pop()
	v, err := vm.loadField(obj, vm.tape.Pool.String(int(in.C)))
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hStoreField(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	val := vm.stack.pop()
	obj := vm.stack.pop()
	u, err := vm.storeField(obj, val, vm.tape.Pool.String(int(in.C)))
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(u)
	return pc + 1, nil
}

func hTryField(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	obj := vm.stack.pop()
	vm.stack.push(vm.tryField(obj, vm.tape.Pool.String(int(in.C))))
	return pc + 1, nil
}

func hLoadIndex(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	idx := vm.stack.pop()
	obj := vm.stack.pop()
	v, err := vm.loadIndex(obj, idx)
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hStoreIndex(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	val := vm.stack.pop()
	idx := vm.stack.pop()
	obj := vm.stack.pop()
	if err := vm.storeIndex(obj, idx, val); err != nil {
		return pc, err.(*RuntimeError)
	}
	return pc + 1, nil
}

func hDup(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(vm.stack.peek())
	return pc + 1, nil
}

func hPop(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.pop()
	return pc + 1, nil
}

func arith2(vm *VM, fr *frame, in Instr, pc int, op Op) (int, *RuntimeError) {
	b := vm.stack.pop()
	a := vm.stack.pop()
	r, err := vm.arith(op, a, b, pc)
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(r)
	return pc + 1, nil
}

func hAdd(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return arith2(vm, fr, in, pc, OpAdd)
}
func hSub(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return arith2(vm, fr, in, pc, OpSub)
}
func hMul(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return arith2(vm, fr, in, pc, OpMul)
}
func hDiv(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return arith2(vm, fr, in, pc, OpDiv)
}
func hMod(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return arith2(vm, fr, in, pc, OpMod)
}

func hNeg(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	a := vm.stack.pop()
	switch a.K {
	case KInt:
		vm.stack.push(IntValue(-a.I))
	case KFloat:
		vm.stack.push(FloatValue(-a.F))
	case KVec2, KVec3, KVec4:
		var o [4]float64
		for i := 0; i < 4; i++ {
			o[i] = -a.V[i]
		}
		vm.stack.push(Value{K: a.K, V: o})
	default:
		return pc, vm.fail("cannot negate %s", a.K)
	}
	return pc + 1, nil
}

func hNot(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.stack.push(BoolValue(!vm.stack.pop().Truthy()))
	return pc + 1, nil
}

func hEq(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	b := vm.stack.pop()
	a := vm.stack.pop()
	vm.stack.push(BoolValue(valueEqual(a, b)))
	return pc + 1, nil
}

func hNe(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	b := vm.stack.pop()
	a := vm.stack.pop()
	vm.stack.push(BoolValue(!valueEqual(a, b)))
	return pc + 1, nil
}

func cmp2(vm *VM, fr *frame, in Instr, pc int, op Op) (int, *RuntimeError) {
	b := vm.stack.pop()
	a := vm.stack.pop()
	r, err := vm.compare(op, a, b)
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(r)
	return pc + 1, nil
}

func hLt(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return cmp2(vm, fr, in, pc, OpLt)
}
func hLe(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return cmp2(vm, fr, in, pc, OpLe)
}
func hGt(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return cmp2(vm, fr, in, pc, OpGt)
}
func hGe(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return cmp2(vm, fr, in, pc, OpGe)
}

func bit2(vm *VM, pc int, op func(a, b int64) int64) (int, *RuntimeError) {
	b := vm.stack.pop()
	a := vm.stack.pop()
	if a.K != KInt || b.K != KInt {
		return pc, vm.fail("bitwise operators need ints, got %s and %s", a.K, b.K)
	}
	vm.stack.push(IntValue(op(a.I, b.I)))
	return pc + 1, nil
}

func hBitAnd(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return bit2(vm, pc, func(a, b int64) int64 { return a & b })
}
func hBitOr(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return bit2(vm, pc, func(a, b int64) int64 { return a | b })
}
func hBitXor(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return bit2(vm, pc, func(a, b int64) int64 { return a ^ b })
}
func hShl(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return bit2(vm, pc, func(a, b int64) int64 {
		if b < 0 || b > 63 {
			return 0
		}
		return a << uint(b)
	})
}
func hShr(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return bit2(vm, pc, func(a, b int64) int64 {
		if b < 0 || b > 63 {
			return 0
		}
		return a >> uint(b)
	})
}

func hJump(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return int(in.A), nil
}
func hJumpIfFalse(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if !vm.stack.pop().Truthy() {
		return int(in.A), nil
	}
	return pc + 1, nil
}
func hJumpIfTrue(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if vm.stack.pop().Truthy() {
		return int(in.A), nil
	}
	return pc + 1, nil
}

// hJumpIfNil implements postfix `?`: a nil result early-returns nil from
// the current function, leaving non-nil values untouched.
func hJumpIfNil(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if vm.stack.peek().IsNil() {
		vm.stack.pop()
		return int(in.A), nil
	}
	return pc + 1, nil
}

// doReturn unwinds one frame, delivering val to the caller.
func (vm *VM) doReturn(fr *frame, val Value) (int, *RuntimeError) {
	for len(vm.iters) > fr.iterBase {
		vm.foreachEnd(fr)
	}
	vm.frames.pop()
	vm.stack.reset(fr.base)
	if fr.ret < 0 {
		vm.stack.push(val)
		return -1, nil
	}
	vm.stack.push(val)
	return fr.ret, nil
}

func hReturn(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	val := vm.stack.pop()
	if len(fr.deferred) > 0 {
		fr.retVal = val
		target := fr.deferred[len(fr.deferred)-1]
		fr.deferred = fr.deferred[:len(fr.deferred)-1]
		return target, nil
	}
	return vm.doReturn(fr, val)
}

func hEndDefer(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if len(fr.deferred) > 0 {
		target := fr.deferred[len(fr.deferred)-1]
		fr.deferred = fr.deferred[:len(fr.deferred)-1]
		return target, nil
	}
	return vm.doReturn(fr, fr.retVal)
}

func hDefer(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	fr.deferred = append(fr.deferred, int(in.A))
	return pc + 1, nil
}

func hCall(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	args := make([]Value, n)
	for i := n - 1; i >= 0; i-- {
		args[i] = vm.stack.pop()
	}
	name := vm.tape.Pool.String(int(in.C))
	var v Value
	var err error
	if name == "" {
		v, err = vm.callValue(vm.stack.pop(), args, vm.budget)
	} else {
		v, err = vm.callNamed(name, args, vm.budget)
	}
	if err != nil {
		if re, ok := err.(*RuntimeError); ok {
			return pc, re
		}
		return pc, &RuntimeError{Msg: err.Error(), Trace: vm.trace()}
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hCallMethod(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	args := make([]Value, n)
	for i := n - 1; i >= 0; i-- {
		args[i] = vm.stack.pop()
	}
	obj := vm.stack.pop()
	v, err := vm.callMethod(obj, vm.tape.Pool.String(int(in.C)), args, vm.budget, pc)
	if err != nil {
		if re, ok := err.(*RuntimeError); ok {
			return pc, re
		}
		return pc, &RuntimeError{Msg: err.Error(), Trace: vm.trace()}
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hMakeArray(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	elems := make([]Value, n)
	for i := n - 1; i >= 0; i-- {
		elems[i] = vm.stack.pop()
	}
	vm.stack.push(Value{K: KArray, O: &ArrayObj{Elems: elems}})
	return pc + 1, nil
}

func hMakeArrayRepeat(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	count := vm.stack.pop()
	elem := vm.stack.pop()
	nf, ok := count.AsFloat()
	if !ok || nf < 0 || nf != float64(int(nf)) {
		return pc, vm.fail("array repeat count must be a non-negative int")
	}
	n := int(nf)
	elems := make([]Value, n)
	for i := range elems {
		elems[i] = elem
	}
	vm.stack.push(Value{K: KArray, O: &ArrayObj{Elems: elems}})
	return pc + 1, nil
}

func hMakeMap(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	fields := make(map[string]Value, n)
	for i := 0; i < n; i++ {
		v := vm.stack.pop()
		k := vm.stack.pop()
		if k.K != KString {
			return pc, vm.fail("map key must be a string")
		}
		fields[k.S] = v
	}
	vm.stack.push(Value{K: KMap, O: &MapObj{Fields: fields}})
	return pc + 1, nil
}

func hMakeStruct(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	pairs := make([]Value, 2*n)
	for i := 2*n - 1; i >= 0; i-- {
		pairs[i] = vm.stack.pop()
	}
	v, err := vm.makeStruct(vm.tape.Pool.String(int(in.C)), pairs)
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hMakeEnum(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	payload := make([]Value, n)
	for i := n - 1; i >= 0; i-- {
		payload[i] = vm.stack.pop()
	}
	full := vm.tape.Pool.String(int(in.C))
	tn, vn, _ := splitVariant(full)
	vm.stack.push(Value{K: KEnum, O: &EnumObj{Type: tn, Variant: vn, Payload: payload}})
	return pc + 1, nil
}

func hLen(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	v, err := vm.lengthOf(vm.stack.pop())
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hForEachBegin(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	comps := make([]string, n)
	muts := make([]bool, n)
	aliases := make([]string, n)
	slots := make([]int32, n)
	for i := n - 1; i >= 0; i-- {
		slots[i] = int32(mustInt(vm.stack.pop()))
		aliases[i] = mustString(vm.stack.pop())
		muts[i] = vm.stack.pop().Truthy()
		comps[i] = mustString(vm.stack.pop())
	}
	wv := vm.stack.pop()
	if wv.K != KWorld {
		return pc, vm.fail("for each iterates a world, got %s", wv.K)
	}
	w, ok := wv.O.(*ecs.World)
	if !ok {
		return pc, vm.fail("for each iterates a world, got %s", wv.K)
	}
	if err := vm.foreachBegin(w, comps, muts, aliases, slots); err != nil {
		return pc, err.(*RuntimeError)
	}
	return pc + 1, nil
}

func mustString(v Value) string {
	if v.K == KString {
		return v.S
	}
	return ""
}

func mustInt(v Value) int64 {
	if f, ok := v.AsFloat(); ok {
		return int64(f)
	}
	return 0
}

func hForEachNext(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	ok, err := vm.foreachNext(fr)
	if err != nil {
		return pc, err.(*RuntimeError)
	}
	if !ok {
		return int(in.A), nil
	}
	return pc + 1, nil
}

func hForEachEnd(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	vm.foreachEnd(fr)
	return pc + 1, nil
}

func hEmit(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if err := vm.emit(vm.stack.pop(), vm.budget); err != nil {
		return pc, err.(*RuntimeError)
	}
	return pc + 1, nil
}

func hBind(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	nw, nwo := int(in.A), int(in.B)
	without := make([]string, nwo)
	for i := nwo - 1; i >= 0; i-- {
		without[i] = mustString(vm.stack.pop())
	}
	with := make([]string, nw)
	for i := nw - 1; i >= 0; i-- {
		with[i] = mustString(vm.stack.pop())
	}
	stage := mustString(vm.stack.pop())
	pipe := mustString(vm.stack.pop())
	iter := vm.stack.pop()
	if iter.K != KWorld {
		return pc, vm.fail("bind applies to a world, got %s", iter.K)
	}
	vm.bindings = append(vm.bindings, RenderBinding{Pipeline: pipe, Stage: stage, With: with, Without: without})
	return pc + 1, nil
}

func hObserve(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	callee := vm.stack.pop()
	cl, ok := callee.O.(*ClosureObj)
	if !ok {
		return pc, vm.fail("observer must be a closure")
	}
	pc2, ok := vm.funcs[cl.Name]
	if !ok {
		return pc, vm.fail("undefined observer %q", cl.Name)
	}
	vm.observers = append(vm.observers, observer{kind: int(in.A), comp: vm.tape.Pool.String(int(in.C)), entry: pc2, up: cl.Up})
	return pc + 1, nil
}

func hMakeClosure(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	n := int(in.A)
	up := make([]Value, n)
	for i := n - 1; i >= 0; i-- {
		up[i] = vm.stack.pop()
	}
	vm.stack.push(Value{K: KClosure, O: &ClosureObj{Name: vm.tape.Pool.String(int(in.C)), Up: up}})
	return pc + 1, nil
}

func hAssert(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	if !vm.stack.pop().Truthy() {
		return pc, vm.fail("assertion failed")
	}
	return pc + 1, nil
}

func hPrint(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	v := vm.stack.pop()
	if vm.stdout != nil {
		vm.stdout.Write([]byte(v.String() + "\n"))
	}
	vm.stack.push(v)
	return pc + 1, nil
}

func hHalt(vm *VM, fr *frame, in Instr, pc int) (int, *RuntimeError) {
	return -1, nil
}
