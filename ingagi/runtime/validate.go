package runtime

import "fmt"

// maxFrameSlots bounds frame-local storage, including missing argument padding.
const maxFrameSlots = 65536

// Module validation.
//
// A tape reaches the VM from two directions: the in-process compiler and
// Decode of a serialized image. Either can hand the dispatch loop an
// instruction stream whose operands disagree with the constant pool, whose
// jump targets leave the code, or whose opcodes have no handler. Those shapes
// used to panic (index out of range, nil handler call) in the middle of a run;
// they are rejected here with a located diagnostic before installation. Stack
// heights are checked across control-flow joins, and type/engine descriptors
// are checked for referential integrity: every name a descriptor references
// (method and hook entries, systems, prefab constructors, event types) must
// resolve inside the module. Opcode-level type verification checks every
// instruction's descriptor operands (struct/enum/closure/call targets and
// enum payload arities) plus unused-operand hygiene. Value-level typing
// remains dynamic at runtime.

// Operand kinds per opcode. The compiler emits these meanings and validation
// enforces them for every instruction, including ones restored by Decode.
const (
	unclassified uint8 = iota // internal error: no rule installed
	noOperands                // the opcode reads only the operand stack
	strC                      // C indexes pool.Strings ("" is the empty-name sentinel)
	intC                      // C indexes pool.Ints
	fltC                      // C indexes pool.Floats
	tgtA                      // A is a code offset that may later be executed
	countA                    // A is a non-negative element/argument/upvalue count
	slotA                     // A is a non-negative local slot or upvalue index
	flagA                     // A is 0 or 1
	obsC                      // C indexes pool.Strings and A is an observer kind (0..2)
	bindAB                    // A/B are with/without counts, C is unused
	foreachA                  // A is a binding count, B/C are unused
)

// opOperands describes each opcode's operand meaning.
var opOperands [256]uint8

func init() {
	set := func(kind uint8, ops ...Op) {
		for _, op := range ops {
			opOperands[op] = kind
		}
	}
	set(intC, OpPushInt)
	set(fltC, OpPushFloat)
	set(strC,
		OpPushString, OpLoadGlobal, OpStoreGlobal, OpLoadField, OpStoreField,
		OpTryField, OpMakeStruct, OpMakeEnum, OpMakeClosure,
		OpCall, OpCallMethod)
	set(tgtA, OpJump, OpJumpIfFalse, OpJumpIfTrue, OpJumpIfNil, OpForEachNext, OpDefer)
	set(countA, OpMakeArray, OpMakeMap)
	set(slotA, OpLoadLocal, OpStoreLocal, OpLoadUp, OpStoreUp)
	set(flagA, OpPushBool, OpReturn)
	// OpObserve uses A for the kind and C for the component name.
	set(obsC, OpObserve)
	// OpBind carries both counts; its C operand is unused (pipeline and stage
	// travel on the stack). OpForEachBegin's world travels on the stack too.
	set(bindAB, OpBind)
	set(foreachA, OpForEachBegin)
	// Opcodes that only move or combine stack values.
	set(noOperands,
		OpPushNil, OpDup, OpPop, OpLoadIndex, OpStoreIndex,
		OpAdd, OpSub, OpMul, OpDiv, OpMod, OpNeg,
		OpEq, OpNe, OpLt, OpLe, OpGt, OpGe, OpNot,
		OpBitAnd, OpBitOr, OpBitXor, OpShl, OpShr, OpLen,
		OpForEachEnd, OpEndDefer, OpEmit, OpAssert, OpPrint, OpHalt, OpMakeArrayRepeat)
}

// Validate reports structural problems in m: a nil tape or pool, unknown
// opcodes, out-of-range pool indices or jump targets, negative counts or
// slots, unused operands that must stay zero, descriptor references that do
// not resolve, enum payload arity mismatches, and entry points that do not
// address code.
func Validate(m *Module) error {
	if m == nil {
		return fmt.Errorf("ingagi: nil module")
	}
	if m.Tape == nil {
		return fmt.Errorf("ingagi: module has no tape")
	}
	if m.Tape.Pool == nil {
		return fmt.Errorf("ingagi: module has no constant pool")
	}
	code := m.Tape.Code
	pool := m.Tape.Pool
	for pc, in := range code {
		if threadedTable[in.Op] == nil {
			return fmt.Errorf("ingagi: pc %d: unknown opcode %d", pc, in.Op)
		}
		switch opOperands[in.Op] {
		case strC:
			if err := checkIndex("string", int(in.C), len(pool.Strings)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
			if in.B != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused B operand must be 0, got %d", pc, OpName(in.Op), in.B)
			}
			switch in.Op {
			case OpPushString, OpLoadGlobal, OpStoreGlobal, OpLoadField,
				OpStoreField, OpTryField:
				if in.A != 0 {
					return fmt.Errorf("ingagi: pc %d %s: unused A operand must be 0, got %d", pc, OpName(in.Op), in.A)
				}
			}
		case intC:
			if err := checkIndex("int", int(in.C), len(pool.Ints)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
			if in.A != 0 || in.B != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got A=%d B=%d", pc, OpName(in.Op), in.A, in.B)
			}
		case fltC:
			if err := checkIndex("float", int(in.C), len(pool.Floats)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
			if in.A != 0 || in.B != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got A=%d B=%d", pc, OpName(in.Op), in.A, in.B)
			}
		case tgtA:
			if err := checkIndex("target", int(in.A), len(code)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
			if in.B != 0 || in.C != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got B=%d C=%d", pc, OpName(in.Op), in.B, in.C)
			}
		case countA, slotA:
			if in.A < 0 {
				return fmt.Errorf("ingagi: pc %d %s: negative operand %d", pc, OpName(in.Op), in.A)
			}
			if opOperands[in.Op] == slotA && in.A >= maxFrameSlots {
				return fmt.Errorf("ingagi: pc %d: slot %d exceeds frame limit", pc, in.A)
			}
			if in.B != 0 || in.C != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got B=%d C=%d", pc, OpName(in.Op), in.B, in.C)
			}
		case flagA:
			if in.A != 0 && in.A != 1 {
				return fmt.Errorf("ingagi: pc %d %s: flag %d is not 0 or 1", pc, OpName(in.Op), in.A)
			}
			if in.B != 0 || in.C != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got B=%d C=%d", pc, OpName(in.Op), in.B, in.C)
			}
		case obsC:
			if err := checkIndex("string", int(in.C), len(pool.Strings)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
			if in.A < 0 || in.A > 2 {
				return fmt.Errorf("ingagi: pc %d %s: observer kind %d out of range", pc, OpName(in.Op), in.A)
			}
			if in.B != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused B operand must be 0, got %d", pc, OpName(in.Op), in.B)
			}
		case bindAB:
			if in.A < 0 || in.B < 0 {
				return fmt.Errorf("ingagi: pc %d %s: negative count", pc, OpName(in.Op))
			}
			if in.C != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused C operand must be 0, got %d", pc, OpName(in.Op), in.C)
			}
		case foreachA:
			if in.A < 0 {
				return fmt.Errorf("ingagi: pc %d %s: negative operand %d", pc, OpName(in.Op), in.A)
			}
			if in.B != 0 || in.C != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got B=%d C=%d", pc, OpName(in.Op), in.B, in.C)
			}
		case noOperands:
			if in.A != 0 || in.B != 0 || in.C != 0 {
				return fmt.Errorf("ingagi: pc %d %s: unused operands must be 0, got A=%d B=%d C=%d", pc, OpName(in.Op), in.A, in.B, in.C)
			}
		default:
			return fmt.Errorf("ingagi: pc %d: opcode %s has no operand rule", pc, OpName(in.Op))
		}
		if err := verifyOpcodeTypes(m, pc, in); err != nil {
			return err
		}
	}
	for name, pc := range m.Tape.Entry {
		if pc < 0 || pc >= len(code) {
			return fmt.Errorf("ingagi: entry %q points at pc %d outside %d instructions", name, pc, len(code))
		}
	}
	for name, n := range m.Params {
		if n < 0 || n > maxFrameSlots {
			return fmt.Errorf("ingagi: entry %q declares %d parameters", name, n)
		}
	}
	for name, ti := range m.Types {
		if ti == nil {
			return fmt.Errorf("ingagi: nil type %q", name)
		}
	}
	if err := verifyStackShapes(m); err != nil {
		return err
	}
	if err := verifyDescriptors(m); err != nil {
		return err
	}
	return nil
}

func checkIndex(what string, i, n int) error {
	if i < 0 || i >= n {
		return fmt.Errorf("%s index %d outside pool of %d", what, i, n)
	}
	return nil
}

// enumPayloadArity reports the declared payload length for Type::Variant.
// known is false when the module carries no signature for the variant (v1
// images decode EnumArity as nil), in which case callers skip the arity
// check but still enforce type/variant existence.
func enumPayloadArity(m *Module, tn, vn string) (arity int, known bool, err error) {
	ti, ok := m.Types[tn]
	if !ok || ti == nil {
		return 0, false, fmt.Errorf("unknown enum type %q", tn)
	}
	if ti.Kind != "enum" {
		return 0, false, fmt.Errorf("%q is %s, not an enum", tn, ti.Kind)
	}
	found := false
	for _, v := range ti.Variants {
		if v == vn {
			found = true
			break
		}
	}
	if !found {
		return 0, false, fmt.Errorf("unknown variant %q::%q", tn, vn)
	}
	if ti.EnumArity == nil {
		return 0, false, nil
	}
	a, ok := ti.EnumArity[vn]
	if !ok {
		return 0, false, fmt.Errorf("variant %q::%q has no payload signature", tn, vn)
	}
	if a < 0 {
		return 0, false, fmt.Errorf("variant %q::%q has negative arity %d", tn, vn, a)
	}
	return a, true, nil
}

// verifyOpcodeTypes checks per-opcode descriptor operands: struct/enum type
// references, closure entry targets, method names and enum payload arities
// for both MakeEnum construction and static Type::Variant calls. Scripts stay
// dynamically relinkable: unknown function/host names are allowed and only
// rejected at call time, but a statically known enum signature must match.
func verifyOpcodeTypes(m *Module, pc int, in Instr) error {
	pool := m.Tape.Pool
	at := func(format string, args ...any) error {
		return fmt.Errorf("ingagi: pc %d %s: %s", pc, OpName(in.Op), fmt.Sprintf(format, args...))
	}
	switch in.Op {
	case OpCall, OpCallMethod, OpMakeStruct, OpMakeEnum, OpMakeClosure:
		if in.A < 0 {
			return at("negative count %d", in.A)
		}
	}
	switch in.Op {
	case OpMakeStruct:
		name := pool.String(int(in.C))
		if name == "" {
			return at("struct type name is empty")
		}
		if ti, ok := m.Types[name]; ok && ti != nil && ti.Kind == "enum" {
			return at("type %q is an enum, not a struct", name)
		}
	case OpMakeEnum:
		full := pool.String(int(in.C))
		tn, vn, ok := splitVariant(full)
		if !ok || tn == "" || vn == "" {
			return at("malformed enum reference %q, want Type::Variant", full)
		}
		arity, known, err := enumPayloadArity(m, tn, vn)
		if err != nil {
			return at("%s", err.Error())
		}
		if known && int(in.A) != arity {
			return at("variant %q takes %d payload values, got %d", full, arity, in.A)
		}
	case OpMakeClosure:
		name := pool.String(int(in.C))
		if name == "" {
			return at("closure entry name is empty")
		}
		if _, ok := m.Tape.Entry[name]; !ok {
			return at("closure references missing entry %q", name)
		}
	case OpCall:
		name := pool.String(int(in.C))
		if name == "" {
			return nil // indirect call through the stack value
		}
		if tn, vn, ok := splitVariant(name); ok && tn != "" && vn != "" {
			if ti, exists := m.Types[tn]; exists && ti != nil && ti.Kind == "enum" {
				arity, known, err := enumPayloadArity(m, tn, vn)
				if err != nil {
					return at("%s", err.Error())
				}
				if known && int(in.A) != arity {
					return at("variant %q takes %d payload values, got %d", name, arity, in.A)
				}
				return nil
			}
		}
		// Missing arguments pad with nil; extras are rejected here so a
		// corrupt image cannot push more values than the callee frame holds.
		if want, ok := m.Params[name]; ok && int(in.A) > want {
			return at("calls %q with %d args, want %d", name, in.A, want)
		}
	case OpCallMethod:
		if pool.String(int(in.C)) == "" {
			return at("method name is empty")
		}
	}
	return nil
}

// verifyStackShapes performs a small abstract interpretation of each entry.
// Values are dynamically typed at runtime, but their stack presence is not:
// every handler has a fixed pop/push shape. Checking that shape at load time
// prevents malformed images from depending on operandStack.pop's nil fallback
// and catches inconsistent branch joins before execution.
func verifyStackShapes(m *Module) error {
	if len(m.Tape.Entry) == 0 || len(m.Tape.Code) == 0 {
		return nil
	}
	for name, start := range m.Tape.Entry {
		states := map[int]int64{start: 0}
		work := []stackEdge{{pc: start, h: 0}}
		for len(work) > 0 {
			e := work[0]
			work = work[1:]
			in := m.Tape.Code[e.pc]
			h := e.h
			pop, push := int64(0), int64(0)
			terminal := false
			add := func(n int64) error {
				if n < 0 {
					return fmt.Errorf("ingagi: entry %q pc %d: invalid stack effect", name, e.pc)
				}
				if h < n {
					return fmt.Errorf("ingagi: entry %q pc %d %s: stack underflow (need %d, have %d)", name, e.pc, OpName(in.Op), n, h)
				}
				h -= n
				return nil
			}
			switch in.Op {
			case OpPushNil, OpPushBool, OpPushInt, OpPushFloat, OpPushString,
				OpLoadLocal, OpLoadGlobal, OpLoadUp:
				push = 1
			case OpStoreLocal, OpStoreGlobal, OpStoreUp:
				pop = 1
			case OpLoadField, OpTryField:
				pop, push = 1, 1
			case OpStoreField:
				pop, push = 2, 1
			case OpLoadIndex:
				pop, push = 2, 1
			case OpStoreIndex:
				pop = 3
			case OpDup:
				pop, push = 1, 2
			case OpPop:
				pop = 1
			case OpAdd, OpSub, OpMul, OpDiv, OpMod,
				OpEq, OpNe, OpLt, OpLe, OpGt, OpGe,
				OpBitAnd, OpBitOr, OpBitXor, OpShl, OpShr:
				pop, push = 2, 1
			case OpNeg, OpNot, OpLen:
				pop, push = 1, 1
			case OpJump:
				if err := enqueueStackEdge(m, states, &work, int(in.A), h, name); err != nil {
					return err
				}
				continue
			case OpJumpIfFalse, OpJumpIfTrue:
				pop = 1
			case OpJumpIfNil:
				// The non-nil path keeps the value; the nil path consumes it in
				// hJumpIfNil before entering the early-return epilogue.
				if h < 1 {
					return fmt.Errorf("ingagi: entry %q pc %d %s: stack underflow (need 1, have %d)", name, e.pc, OpName(in.Op), h)
				}
				if err := enqueueStackEdge(m, states, &work, int(in.A), h-1, name); err != nil {
					return err
				}
				if err := enqueueStackEdge(m, states, &work, e.pc+1, h, name); err != nil {
					return err
				}
				continue
			case OpReturn:
				pop = 1
				terminal = true
			case OpCall:
				n := int64(in.A)
				if n < 0 {
					return fmt.Errorf("ingagi: entry %q pc %d call has negative arity", name, e.pc)
				}
				if callee := m.Tape.Pool.String(int(in.C)); callee != "" {
					if want, ok := m.Params[callee]; ok && int(n) > want {
						return fmt.Errorf("ingagi: entry %q pc %d calls %q with %d args, want %d", name, e.pc, callee, n, want)
					}
					pop, push = n, 1
				} else {
					pop, push = n+1, 1 // callee value plus arguments
				}
			case OpCallMethod:
				n := int64(in.A)
				if n < 0 {
					return fmt.Errorf("ingagi: entry %q pc %d method call has negative arity", name, e.pc)
				}
				pop, push = n+1, 1 // receiver plus arguments
			case OpMakeArray:
				pop, push = int64(in.A), 1
			case OpMakeArrayRepeat:
				pop, push = 2, 1
			case OpMakeMap:
				pop, push = 2*int64(in.A), 1
			case OpMakeStruct:
				pop, push = 2*int64(in.A), 1
			case OpMakeEnum:
				pop, push = int64(in.A), 1
			case OpMakeClosure:
				pop, push = int64(in.A), 1
			case OpForEachBegin:
				pop = 4*int64(in.A) + 1
			case OpForEachNext, OpForEachEnd:
				if in.Op == OpForEachNext {
					if err := enqueueStackEdge(m, states, &work, int(in.A), h, name); err != nil {
						return err
					}
				}
			case OpEmit, OpAssert, OpObserve:
				pop = 1
			case OpBind:
				pop = int64(in.A) + int64(in.B) + 3
			case OpDefer:
				// Return removes its result and discards temporary operands before
				// executing deferred blocks. Registration-time operands aren't live.
				if err := enqueueStackEdge(m, states, &work, int(in.A), 0, name); err != nil {
					return err
				}
			case OpEndDefer, OpHalt:
				continue
			case OpPrint:
				pop, push = 1, 1
			default:
				return fmt.Errorf("ingagi: entry %q pc %d: no stack rule for %s", name, e.pc, OpName(in.Op))
			}
			if err := add(pop); err != nil {
				return err
			}
			h += push
			if terminal {
				continue
			}
			if in.Op == OpJumpIfFalse || in.Op == OpJumpIfTrue {
				if err := enqueueStackEdge(m, states, &work, int(in.A), h, name); err != nil {
					return err
				}
			}
			if e.pc+1 >= len(m.Tape.Code) {
				return fmt.Errorf("ingagi: entry %q pc %d %s falls off the tape", name, e.pc, OpName(in.Op))
			}
			if err := enqueueStackEdge(m, states, &work, e.pc+1, h, name); err != nil {
				return err
			}
		}
	}
	return nil
}

type stackEdge struct {
	pc int
	h  int64
}

func enqueueStackEdge(m *Module, states map[int]int64, work *[]stackEdge, pc int, h int64, entry string) error {
	if pc < 0 || pc >= len(m.Tape.Code) {
		return fmt.Errorf("ingagi: entry %q reaches pc %d outside tape", entry, pc)
	}
	if old, ok := states[pc]; ok {
		if old != h {
			return fmt.Errorf("ingagi: entry %q pc %d: inconsistent stack height %d versus %d", entry, pc, old, h)
		}
		return nil
	}
	states[pc] = h
	*work = append(*work, stackEdge{pc: pc, h: h})
	return nil
}
