package runtime

// requiredOperands is the minimum number of activation-local operands read by
// a handler. Keep the peek operations here too: Dup and JumpIfNil don't always
// consume their operand but must never read a caller's temporary stack values.
func requiredOperands(in Instr, pool *ConstPool) int64 {
	switch in.Op {
	case OpStoreLocal, OpStoreGlobal, OpStoreUp, OpLoadField, OpTryField,
		OpDup, OpPop, OpNeg, OpNot, OpLen, OpJumpIfTrue, OpJumpIfFalse,
		OpJumpIfNil, OpReturn, OpEmit, OpAssert, OpObserve, OpPrint:
		return 1
	case OpStoreField, OpLoadIndex, OpAdd, OpSub, OpMul, OpDiv, OpMod,
		OpEq, OpNe, OpLt, OpLe, OpGt, OpGe, OpBitAnd, OpBitOr, OpBitXor,
		OpShl, OpShr, OpMakeArrayRepeat:
		return 2
	case OpStoreIndex:
		return 3
	case OpCall:
		if pool.String(int(in.C)) == "" {
			return int64(in.A) + 1
		}
		return int64(in.A)
	case OpCallMethod:
		return int64(in.A) + 1
	case OpMakeArray, OpMakeEnum, OpMakeClosure:
		return int64(in.A)
	case OpMakeMap, OpMakeStruct:
		return 2 * int64(in.A)
	case OpForEachBegin:
		return 4*int64(in.A) + 1
	case OpBind:
		return int64(in.A) + int64(in.B) + 3
	default:
		return 0
	}
}
