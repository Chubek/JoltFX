// Bytecode opcodes for the Ingagi VM.
//
// The tape is a flat instruction stream executed by dispatch.go. Operands
// are decoded inline (A = slot/index, B = secondary, C = constant pool
// index). Control flow uses absolute jump targets patched by the compiler,
// so the tape stays serializable (see aot.go).
package runtime

import "strconv"

// Op is a single opcode.
type Op byte

const (
	OpInvalid Op = iota
	// Constants and data movement.
	OpPushNil
	OpPushBool // A: 0/1
	OpPushInt  // C: int64 pool index
	OpPushFloat
	OpPushString
	OpLoadLocal  // A: slot
	OpStoreLocal // A: slot
	OpLoadGlobal // C: name index
	OpStoreGlobal
	OpLoadField  // C: field name index
	OpStoreField // C: field name index
	OpLoadIndex
	OpStoreIndex
	OpDup
	OpPop
	// Arithmetic.
	OpAdd
	OpSub
	OpMul
	OpDiv
	OpMod
	OpNeg
	// Comparison.
	OpEq
	OpNe
	OpLt
	OpLe
	OpGt
	OpGe
	// Logic (non-short-circuit primitives; the compiler emits jumps).
	OpNot
	// Bitwise.
	OpBitAnd
	OpBitOr
	OpBitXor
	OpShl
	OpShr
	// Control flow.
	OpJump            // A: target
	OpJumpIfFalse     // A: target (pops)
	OpJumpIfTrue      // A: target (pops)
	OpJumpIfNil       // A: target (peeks; used by `?`)
	OpReturn          // A: 0 void, 1 value
	OpCall            // A: argc, C: name index ("" = call callee on stack)
	OpCallMethod      // A: argc, C: method name index
	OpMakeArray       // A: count (pops count)
	OpMakeArrayRepeat // (pops count, elem)
	OpMakeMap         // A: count (pops 2*count)
	OpMakeStruct      // A: field count, C: type name index
	OpMakeEnum        // A: payload count, C: "Type::Variant" index
	OpLen
	OpForEachBegin // A: binding count, C: world-global index or "" (uses stack)
	OpForEachNext  // A: end target
	OpForEachEnd
	OpEmit  // (pops event struct)
	OpBind  // A: with-count, B: without-count, C: pipeline index triple
	OpDefer // A: target (registers deferred call frame)
	OpEndDefer
	OpObserve   // A: 0 add, 1 remove, 2 set; C: component name index (pops closure)
	OpTryField  // C: field name index (missing field -> nil, never errors)
	OpMakeClosure // A: upvalue count, C: entry name index (pops A values)
	OpLoadUp      // A: upvalue index
	OpStoreUp     // A: upvalue index
	OpAssert
	OpPrint
	OpHalt
)

// OpName renders an opcode for disassembly and traces.
func OpName(op Op) string {
	switch op {
	case OpPushNil:
		return "push_nil"
	case OpPushBool:
		return "push_bool"
	case OpPushInt:
		return "push_int"
	case OpPushFloat:
		return "push_float"
	case OpPushString:
		return "push_string"
	case OpLoadLocal:
		return "load_local"
	case OpStoreLocal:
		return "store_local"
	case OpLoadGlobal:
		return "load_global"
	case OpStoreGlobal:
		return "store_global"
	case OpLoadField:
		return "load_field"
	case OpStoreField:
		return "store_field"
	case OpLoadIndex:
		return "load_index"
	case OpStoreIndex:
		return "store_index"
	case OpDup:
		return "dup"
	case OpPop:
		return "pop"
	case OpAdd:
		return "add"
	case OpSub:
		return "sub"
	case OpMul:
		return "mul"
	case OpDiv:
		return "div"
	case OpMod:
		return "mod"
	case OpNeg:
		return "neg"
	case OpEq:
		return "eq"
	case OpNe:
		return "ne"
	case OpLt:
		return "lt"
	case OpLe:
		return "le"
	case OpGt:
		return "gt"
	case OpGe:
		return "ge"
	case OpNot:
		return "not"
	case OpBitAnd:
		return "bitand"
	case OpBitOr:
		return "bitor"
	case OpBitXor:
		return "bitxor"
	case OpShl:
		return "shl"
	case OpShr:
		return "shr"
	case OpJump:
		return "jump"
	case OpJumpIfFalse:
		return "jump_false"
	case OpJumpIfTrue:
		return "jump_true"
	case OpJumpIfNil:
		return "jump_nil"
	case OpReturn:
		return "return"
	case OpCall:
		return "call"
	case OpCallMethod:
		return "call_method"
	case OpMakeArray:
		return "make_array"
	case OpMakeArrayRepeat:
		return "make_repeat"
	case OpMakeMap:
		return "make_map"
	case OpMakeStruct:
		return "make_struct"
	case OpMakeEnum:
		return "make_enum"
	case OpLen:
		return "len"
	case OpForEachBegin:
		return "foreach_begin"
	case OpForEachNext:
		return "foreach_next"
	case OpForEachEnd:
		return "foreach_end"
	case OpEmit:
		return "emit"
	case OpBind:
		return "bind"
	case OpDefer:
		return "defer"
	case OpEndDefer:
		return "end_defer"
	case OpObserve:
		return "observe"
	case OpTryField:
		return "try_field"
	case OpMakeClosure:
		return "make_closure"
	case OpLoadUp:
		return "load_up"
	case OpStoreUp:
		return "store_up"
	case OpAssert:
		return "assert"
	case OpPrint:
		return "print"
	case OpHalt:
		return "halt"
	}
	return "invalid"
}

// Instr is one tape instruction.
type Instr struct {
	Op Op
	A  int32
	B  int32
	C  int32
}

// Disassemble renders one instruction with pool resolution.
func (in Instr) Disassemble(pool *ConstPool) string {
	op := OpName(in.Op)
	switch in.Op {
	case OpPushInt:
		return op + " " + itoa(pool.Int(int(in.C)))
	case OpPushFloat:
		return op + " " + ftoa(pool.Float(int(in.C)))
	case OpPushString, OpLoadGlobal, OpStoreGlobal, OpLoadField,
		OpStoreField, OpMakeStruct, OpMakeEnum, OpBind:
		return op + " " + pool.String(int(in.C))
	case OpPushBool:
		if in.A == 0 {
			return op + " false"
		}
		return op + " true"
	case OpJump, OpJumpIfFalse, OpJumpIfTrue, OpJumpIfNil, OpDefer:
		return op + " @" + itoa(int64(in.A))
	case OpEndDefer:
		return op
	case OpObserve:
		return op + " " + pool.String(int(in.C))
	case OpTryField:
		return op + " " + pool.String(int(in.C))
	case OpMakeClosure:
		return op + " n=" + itoa(int64(in.A)) + " " + pool.String(int(in.C))
	case OpLoadUp, OpStoreUp:
		return op + " ^" + itoa(int64(in.A))
	case OpCall, OpCallMethod:
		return op + " argc=" + itoa(int64(in.A)) + " " + pool.String(int(in.C))
	case OpLoadLocal, OpStoreLocal:
		return op + " $" + itoa(int64(in.A))
	case OpMakeArray, OpMakeMap, OpReturn:
		return op + " " + itoa(int64(in.A))
	case OpForEachBegin:
		return op + " n=" + itoa(int64(in.A))
	case OpForEachNext:
		return op + " @" + itoa(int64(in.A))
	default:
		return op
	}
}

func itoa(i int64) string {
	if i == 0 {
		return "0"
	}
	neg := i < 0
	if neg {
		i = -i
	}
	var b [20]byte
	p := len(b)
	for i > 0 {
		p--
		b[p] = byte('0' + i%10)
		i /= 10
	}
	if neg {
		p--
		b[p] = '-'
	}
	return string(b[p:])
}

func ftoa(f float64) string { return strconv.FormatFloat(f, 'g', -1, 64) }
