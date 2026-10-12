package runtime

import (
	"bytes"
	"testing"
)

func TestReloadReplacesScriptState(t *testing.T) {
	vm := New()
	old := compileMain(t, `const Stale: i32 = 1; fn main(a: i32, b: i32) { return a; }`)
	if err := vm.LoadModule(old); err != nil {
		t.Fatal(err)
	}
	vm.profiles[0] = &opProfile{}
	vm.caches[0] = &inlineCache{}
	next := compileMain(t, `fn main() { return 42; }`)
	if err := vm.LoadModule(next); err != nil {
		t.Fatal(err)
	}
	if _, exists := vm.globals["Stale"]; exists {
		t.Fatal("stale global survived reload")
	}
	if len(vm.caches) != 0 || len(vm.profiles) != 0 {
		t.Fatal("stale optimization state survived reload")
	}
	if vm.params["main"] != 0 {
		t.Fatal("stale arity survived reload")
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 42 {
		t.Fatalf("call: %v %v", v, err)
	}
}

func TestImageEncodingDeterministicAndLosslessSubset(t *testing.T) {
	vm := New()
	m := compileMain(t, `struct P { x: i32; } enum S { A, B } fn f() { return 1; } fn main() { return 42; }`)
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	a, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 8; i++ {
		b, err := vm.Encode()
		if err != nil || !bytes.Equal(a, b) {
			t.Fatalf("nondeterministic encoding: %v", err)
		}
	}
	d, err := Decode(a)
	if err != nil {
		t.Fatal(err)
	}
	if err := vm.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	b, err := vm.Encode()
	if err != nil || !bytes.Equal(a, b) {
		t.Fatalf("roundtrip changed encoding: %v", err)
	}
}

func TestEncodingRejectsLossyAndCyclicValues(t *testing.T) {
	vm := New()
	if err := vm.LoadModule(compileMain(t, `fn main() { return 42; }`)); err != nil {
		t.Fatal(err)
	}
	vm.globals["captured"] = Value{K: KClosure, O: &ClosureObj{Name: "main", Up: []Value{IntValue(1)}}}
	if _, err := vm.Encode(); err == nil {
		t.Fatal("silently discarded captured environment")
	}
	delete(vm.globals, "captured")
	a := &ArrayObj{}
	v := Value{K: KArray, O: a}
	a.Elems = []Value{v}
	vm.globals["cycle"] = v
	if _, err := vm.Encode(); err == nil {
		t.Fatal("accepted cyclic snapshot")
	}
}

func TestFrameAllocationBounds(t *testing.T) {
	m := compileMain(t, `fn main() { return 42; }`)
	m.Params["main"] = 1 << 30
	if err := New().LoadModule(m); err == nil {
		t.Fatal("accepted excessive arity")
	}
	m.Params["main"] = 0
	m.Tape.Code[m.Tape.Entry["main"]] = Instr{Op: OpStoreLocal, A: 1 << 30}
	if err := New().LoadModule(m); err == nil {
		t.Fatal("accepted excessive local slot")
	}
}
