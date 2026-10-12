package module_system

import (
	"bytes"
	"strings"
	"testing"
	"testing/fstest"

	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func TestImportsExecute(t *testing.T) {
	fs := fstest.MapFS{
		"main.ing":  {Data: []byte(`import math as m; import other; fn main() { let double_it = |x: i64| x + 1; return m::double_it(other::value()) + double_it(1); }`)},
		"math.ing":  {Data: []byte(`module math; const factor = 2; public fn double_it(x: i64) { return x * factor; }`)},
		"other.ing": {Data: []byte(`module other; import math; public fn value() { return math::double_it(10); }`)},
	}
	f, err := (&Loader{FS: fs}).Load("main.ing")
	if err != nil {
		t.Fatal(err)
	}
	m, errs := rt.Compile(f)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	vm := rt.New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 42 {
		t.Fatalf("got %v, %v", v, err)
	}
}

func TestDiamondInitializesOnce(t *testing.T) {
	fs := fstest.MapFS{
		"main.ing":   {Data: []byte(`import left; import right; fn main() { return left::get() + right::get(); }`)},
		"left.ing":   {Data: []byte(`import shared; public fn get() { return shared::value; }`)},
		"right.ing":  {Data: []byte(`import shared; public fn get() { return shared::value; }`)},
		"shared.ing": {Data: []byte(`fn initialize() { print("init"); return 21; } public const value = initialize();`)},
	}
	f, err := (&Loader{FS: fs}).Load("main.ing")
	if err != nil {
		t.Fatal(err)
	}
	m, errs := rt.Compile(f)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	vm := rt.New()
	var out bytes.Buffer
	vm.SetStdout(&out)
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 42 || out.String() != "init\n" {
		t.Fatalf("%v, %v, %q", v, err, out.String())
	}
}

func TestImportFailures(t *testing.T) {
	for _, tc := range []struct{ name, main, dep, want string }{
		{"missing", `import absent; fn main() {}`, ``, "absent"},
		{"cycle", `module main; import dep;`, `module dep; import main;`, "cycle"},
		{"private", `import dep; fn main() { return dep::hidden(); }`, `private fn hidden() { return 1; }`, "not exported"},
		{"module", `import dep;`, `module wrong;`, "declares module"},
		{"alias", `import dep as d; import dep as d;`, `public fn f() {}`, "duplicate import"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			fs := fstest.MapFS{"main.ing": {Data: []byte(tc.main)}, "dep.ing": {Data: []byte(tc.dep)}}
			_, err := (&Loader{FS: fs}).Load("main.ing")
			if err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("got %v, want %s", err, tc.want)
			}
		})
	}
}

func TestLexicalScopesAndTypes(t *testing.T) {
	fs := fstest.MapFS{
		"main.ing": {Data: []byte(`import dep as d; fn main() { let p = d::Point { x: 5 }; return p.get() + d::value(); }`)},
		"dep.ing":  {Data: []byte(`public struct Point { x: i64; fn get(self: Point) { return self.x; } } const x = 7; public fn value() { { let x = 9; } return x; }`)},
	}
	f, err := (&Loader{FS: fs}).Load("main.ing")
	if err != nil {
		t.Fatal(err)
	}
	m, errs := rt.Compile(f)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	vm := rt.New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.Call("main")
	if err != nil || v.I != 12 {
		t.Fatalf("got %v, %v", v, err)
	}
}
