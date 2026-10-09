package ingagi_test

import (
	"testing"

	"github.com/Chubek/JoltFX/ingagi"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func TestEmbeddingHostFunction(t *testing.T) {
	m, err := ingagi.Compile(`fn main() { return host_double(21); }`)
	if err != nil {
		t.Fatal(err)
	}
	vm := rt.New()
	vm.SetBudget(rt.Budget{MaxInstrs: 100})
	vm.RegisterHostFn("host_double", func(_ *rt.VM, args []rt.Value) (rt.Value, error) {
		return rt.IntValue(args[0].I * 2), nil
	})
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 42 {
		t.Fatalf("got %v, %v", v, err)
	}
}

func TestCompileRejectsErrors(t *testing.T) {
	for _, src := range []string{`fn broken( {`, `fn main() { break; }`} {
		if m, err := ingagi.Compile(src); err == nil || m != nil {
			t.Fatalf("accepted %q", src)
		}
	}
}
