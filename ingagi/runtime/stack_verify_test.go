package runtime

import (
	"strings"
	"testing"
)

func TestVerifierVisitsBothConditionalEdges(t *testing.T) {
	for _, op := range []Op{OpJumpIfTrue, OpJumpIfFalse} {
		t.Run(OpName(op), func(t *testing.T) {
			m := &Module{Tape: NewTape()}
			m.Tape.Entry["main"] = 0
			m.Tape.Code = []Instr{{Op: OpPushBool}, {Op: op, A: 3}, {Op: OpHalt}, {Op: OpPop}, {Op: OpHalt}}
			if err := Validate(m); err == nil || !strings.Contains(err.Error(), "underflow") {
				t.Fatalf("unchecked taken edge: %v", err)
			}
		})
	}
}

func TestVerifierRejectsConditionalHeightMismatch(t *testing.T) {
	m := &Module{Tape: NewTape()}
	m.Tape.Entry["main"] = 0
	m.Tape.Code = []Instr{{Op: OpPushBool}, {Op: OpJumpIfTrue, A: 3}, {Op: OpPushNil}, {Op: OpHalt}}
	if err := Validate(m); err == nil || !strings.Contains(err.Error(), "inconsistent") {
		t.Fatalf("unchecked branch join: %v", err)
	}
}

func TestVerifierAllowsMissingArguments(t *testing.T) {
	m := &Module{Tape: NewTape(), Params: map[string]int{"f": 1}}
	m.Tape.Entry["main"], m.Tape.Entry["f"] = 0, 2
	name := m.Tape.Pool.InternString("f")
	m.Tape.Code = []Instr{{Op: OpCall, C: name}, {Op: OpReturn}, {Op: OpLoadLocal}, {Op: OpReturn}}
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	if v, err := vm.Call("main"); err != nil || !v.IsNil() {
		t.Fatalf("missing argument should be padded with nil: %v, %v", v, err)
	}
}

func TestVerifierChecksDeferredBodyAtReturnHeight(t *testing.T) {
	m := &Module{Tape: NewTape()}
	m.Tape.Entry["main"] = 0
	m.Tape.Code = []Instr{{Op: OpPushNil}, {Op: OpDefer, A: 3}, {Op: OpReturn}, {Op: OpPop}, {Op: OpEndDefer}}
	if err := Validate(m); err == nil || !strings.Contains(err.Error(), "underflow") {
		t.Fatalf("defer borrowed a registration-time operand: %v", err)
	}
}

func TestRuntimeUnderflowDoesNotBorrowCallerOperands(t *testing.T) {
	pool := NewConstPool()
	name := pool.InternString("")
	for _, in := range []Instr{{Op: OpPop}, {Op: OpDup}, {Op: OpAdd}, {Op: OpLoadIndex}, {Op: OpCall, C: name}} {
		vm := New()
		vm.tape = &Tape{Code: []Instr{in, {Op: OpHalt}}, Pool: pool}
		vm.stack.push(IntValue(42))
		if err := vm.frames.push(frame{base: 1}); err != nil {
			t.Fatal(err)
		}
		if _, err := vm.run(0, Budget{MaxInstrs: 10}); err == nil {
			t.Fatalf("%s read its caller's operand", OpName(in.Op))
		}
		if vm.stack.depth() != 1 || vm.stack.peek().I != 42 {
			t.Fatalf("%s damaged caller stack", OpName(in.Op))
		}
	}
}
