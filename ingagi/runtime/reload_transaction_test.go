package runtime

import (
	"bytes"
	"testing"
)

func TestFailedInitializerPreservesModule(t *testing.T) {
	vm := New()
	old := compileMain(t, `const x = 42; fn main() { return x; }`)
	if err := vm.LoadModule(old); err != nil {
		t.Fatal(err)
	}
	before, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	next := compileMain(t, `const x = 7; const bad = assert(false); fn replacement() { return x; }`)
	if err := vm.LoadModule(next); err == nil {
		t.Fatal("initializer unexpectedly succeeded")
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 42 {
		t.Fatalf("lost old module: %v, %v", v, err)
	}
	if _, err := vm.Call("replacement"); err == nil {
		t.Fatal("failed module entry survived")
	}
	after, err := vm.Encode()
	if err != nil || !bytes.Equal(before, after) {
		t.Fatalf("snapshot changed: %v", err)
	}
}

func TestReloadRejectedDuringExecution(t *testing.T) {
	vm := New()
	next := compileMain(t, `fn main() { return 7; }`)
	vm.RegisterHostFn("reload", func(vm *VM, _ []Value) (Value, error) {
		if err := vm.LoadModule(next); err == nil {
			t.Error("allowed active reload")
		}
		return IntValue(42), nil
	})
	if err := vm.LoadModule(compileMain(t, `fn main() { return reload(); }`)); err != nil {
		t.Fatal(err)
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 42 {
		t.Fatalf("%v, %v", v, err)
	}
}
