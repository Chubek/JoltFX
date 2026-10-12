package ingagi_test

import (
	"context"
	"os"
	"strings"
	"testing"
	"testing/fstest"

	"github.com/Chubek/JoltFX/ingagi"
	"github.com/Chubek/JoltFX/ingagi/compiler"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func TestNativeEmbedding(t *testing.T) {
	data, err := ingagi.CompileNative(context.Background(), `fn main() -> i64 { return 42; }`, ingagi.NativeOptions{Emit: "qbe"})
	if err != nil || !strings.Contains(string(data), "export function l $ingagi_entry") {
		t.Fatalf("got %s, %v", data, err)
	}
}

func TestJITEmbeddingImports(t *testing.T) {
	if _, err := ingagi.CompileJIT(context.Background(), "fn broken(", ingagi.NativeOptions{}); err == nil {
		t.Fatal("accepted malformed source")
	}
	if _, err := ingagi.CompileJITFile(context.Background(), fstest.MapFS{}, "missing.ing", ingagi.NativeOptions{}); err == nil {
		t.Fatal("accepted missing source")
	}
	if !compiler.JITAvailable() || os.Getenv("INGAGI_QBE") == "" || os.Getenv("INGAGI_KEYSTONE_LIBRARY") == "" {
		t.Skip("requires native JIT tools/platform")
	}
	sources := fstest.MapFS{
		"main.ing": {Data: []byte(`import lib; fn main() -> i64 { return lib::twice(21); }`)},
		"lib.ing":  {Data: []byte(`public fn twice(x:i64) -> i64 { return x*2; }`)},
	}
	fn, err := ingagi.CompileJITFile(context.Background(), sources, "main.ing", ingagi.NativeOptions{})
	if err != nil {
		t.Fatal(err)
	}
	defer fn.Close()
	if got, err := fn.Call(); err != nil || got != 42 {
		t.Fatalf("%d: %v", got, err)
	}
}

func TestNativeImports(t *testing.T) {
	fs := fstest.MapFS{
		"main.ing": {Data: []byte(`import lib; fn main() -> i64 { return lib::twice(21); }`)},
		"lib.ing":  {Data: []byte(`public fn twice(x: i64) -> i64 { return x * 2; }`)},
	}
	data, err := ingagi.CompileNativeFile(context.Background(), fs, "main.ing", ingagi.NativeOptions{Emit: "qbe"})
	if err != nil || !strings.Contains(string(data), "call $ig_fn_0(l 21)") {
		t.Fatalf("%s: %v", data, err)
	}
}

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
