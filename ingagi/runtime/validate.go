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
// shape, all descriptor semantics and resource consumption are not verified.

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
		OpTryField, OpMakeStruct, OpMakeEnum, OpBind, OpMakeClosure,
		OpCall, OpCallMethod, OpForEachBegin)
	set(tgtA, OpJump, OpJumpIfFalse, OpJumpIfTrue, OpJumpIfNil, OpForEachNext, OpDefer)
	set(countA, OpMakeArray, OpMakeMap)
	set(slotA, OpLoadLocal, OpStoreLocal, OpLoadUp, OpStoreUp)
	set(flagA, OpPushBool, OpReturn)
	// OpObserve uses A for the kind and C for the component name.
	set(obsC, OpObserve)
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
// slots, and entry points that do not address code.
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
		case intC:
			if err := checkIndex("int", int(in.C), len(pool.Ints)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
		case fltC:
			if err := checkIndex("float", int(in.C), len(pool.Floats)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
		case tgtA:
			if err := checkIndex("target", int(in.A), len(code)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
		case countA, slotA:
			if in.A < 0 {
				return fmt.Errorf("ingagi: pc %d %s: negative operand %d", pc, OpName(in.Op), in.A)
			}
			if opOperands[in.Op] == slotA && in.A >= maxFrameSlots {
				return fmt.Errorf("ingagi: pc %d: slot %d exceeds frame limit", pc, in.A)
			}
		case flagA:
			if in.A != 0 && in.A != 1 {
				return fmt.Errorf("ingagi: pc %d %s: flag %d is not 0 or 1", pc, OpName(in.Op), in.A)
			}
		case obsC:
			if err := checkIndex("string", int(in.C), len(pool.Strings)); err != nil {
				return fmt.Errorf("ingagi: pc %d %s: %w", pc, OpName(in.Op), err)
			}
			if in.A < 0 || in.A > 2 {
				return fmt.Errorf("ingagi: pc %d %s: observer kind %d out of range", pc, OpName(in.Op), in.A)
			}
		case noOperands:
		default:
			return fmt.Errorf("ingagi: pc %d: opcode %s has no operand rule", pc, OpName(in.Op))
		}
		switch in.Op {
		case OpCall, OpCallMethod, OpMakeStruct, OpMakeEnum, OpMakeClosure, OpForEachBegin, OpBind:
			if in.A < 0 || in.Op == OpBind && in.B < 0 {
				return fmt.Errorf("ingagi: pc %d %s: negative count", pc, OpName(in.Op))
			}
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
	return nil
}

func checkIndex(what string, i, n int) error {
	if i < 0 || i >= n {
		return fmt.Errorf("%s index %d outside pool of %d", what, i, n)
	}
	return nil
}
