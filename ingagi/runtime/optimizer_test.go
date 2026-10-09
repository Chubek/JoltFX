package runtime

import (
	"bytes"
	"reflect"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func TestOptimizerRelocatesEntriesAndDefers(t *testing.T) {
	r := fe.Parse(`fn unused() { 123; return 0; }
        fn main() { defer { 456; print("done"); } 789; return 42; }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	original := append([]Instr(nil), m.Tape.Code...)
	optimized := Optimize(m.Tape)
	if !reflect.DeepEqual(original, m.Tape.Code) {
		t.Fatal("optimizer changed input tape")
	}
	if len(optimized.Code) >= len(original) {
		t.Fatal("dead push/pop pairs were not removed")
	}
	m.Tape = optimized
	vm := New()
	var out bytes.Buffer
	vm.SetStdout(&out)
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.CallBudget(Budget{MaxInstrs: 1000}, "main")
	if err != nil {
		t.Fatal(err)
	}
	if v.I != 42 || out.String() != "done\n" {
		t.Fatalf("got %v, output %q", v, out.String())
	}
}

func TestOptimizerPreservesJumpCyclesAndHaltEntries(t *testing.T) {
	tape := &Tape{Pool: NewConstPool(), Entry: map[string]int{"loop": 0, "after": 3},
		Code: []Instr{{Op: OpJump, A: 1}, {Op: OpJump, A: 0}, {Op: OpHalt},
			{Op: OpPushNil}, {Op: OpReturn}}}
	got := Optimize(tape)
	if !reflect.DeepEqual(got, tape) {
		t.Fatalf("changed cyclic control flow or entry after halt: %+v", got)
	}
}

func TestOptimizerProtectsLandingPads(t *testing.T) {
	for _, op := range []Op{OpDefer, OpJump, OpJumpIfNil, OpForEachNext} {
		tape := &Tape{Pool: NewConstPool(), Entry: map[string]int{"start": 0},
			Code: []Instr{{Op: op, A: 2}, {Op: OpPushNil}, {Op: OpPop}, {Op: OpReturn}}}
		if got := Optimize(tape); len(got.Code) != len(tape.Code) {
			t.Fatalf("removed targeted pop for %v", op)
		}
	}
}
