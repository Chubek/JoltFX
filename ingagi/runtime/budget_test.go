package runtime

import (
	"strings"
	"testing"

	"github.com/Chubek/JoltFX/ingagi/ecs"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func TestBudgetSpansNestedCallsAndVMRecovers(t *testing.T) {
	r := fe.Parse(`fn work() { let i = 0; while i < 100 { i = i + 1; } return i; }
        fn main() { return work(); }
        fn answer() { return 42; }
        fn fail() { return 1 + missing; }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	_, err := vm.CallBudget(Budget{MaxInstrs: 30}, "main")
	if err == nil || !strings.Contains(err.Error(), "instruction budget exceeded") {
		t.Fatalf("got %v", err)
	}
	if vm.frames.depth() != 0 || vm.stack.depth() != 0 {
		t.Fatal("failed call leaked frames/operands")
	}
	for i := 0; i < 20; i++ {
		v, err := vm.CallBudget(Budget{MaxInstrs: 3}, "answer")
		if err != nil || v.I != 42 {
			t.Fatalf("call %d: %v, %v", i, v, err)
		}
	}
	_, err = vm.Call("fail")
	if err == nil {
		t.Fatal("expected undefined name")
	}
	if vm.frames.depth() != 0 || vm.stack.depth() != 0 {
		t.Fatal("runtime error leaked frames/operands")
	}
}

func TestIteratorUnwind(t *testing.T) {
	r := fe.Parse(`fn first(w: World) {
            for each (&mut Pos as p) in w { p.x = p.x + 1; return p.x; }
        }
        fn outer(w: World) {
            for each (&Pos as p) in w { return first(w); }
        }
        fn fail(w: World) {
            for each (&mut Pos as p) in w { p.x = 99; return missing; }
        }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	w := ecs.NewWorld()
	id := w.Spawn(map[string]map[string]any{"Pos": {"x": 1}})
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.CallBudget(Budget{MaxInstrs: 1000}, "outer", worldValue(w))
	if err != nil || v.I != 2 {
		t.Fatalf("got %v, %v", v, err)
	}
	fields, _ := w.Get(id, "Pos")
	if fromAny(fields["x"]).I != 2 || len(vm.iters) != 0 {
		t.Fatalf("early return failed to flush/close iterator: %v, %d", fields, len(vm.iters))
	}
	if _, err := vm.Call("fail", worldValue(w)); err == nil {
		t.Fatal("expected undefined name")
	}
	fields, _ = w.Get(id, "Pos")
	if fromAny(fields["x"]).I != 2 || len(vm.iters) != 0 {
		t.Fatalf("error failed to discard unfinished iterator: %v, %d", fields, len(vm.iters))
	}
}

func TestFrameBudgetSpansNestedCalls(t *testing.T) {
	r := fe.Parse(`fn f() { return f(); }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, _ := Compile(r.File)
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	_, err := vm.CallBudget(Budget{MaxFrames: 3, MaxInstrs: 100}, "f")
	if err == nil || !strings.Contains(err.Error(), "call depth > 3") {
		t.Fatalf("got %v", err)
	}
	if vm.frames.depth() != 0 {
		t.Fatal("stack was not unwound")
	}
}
