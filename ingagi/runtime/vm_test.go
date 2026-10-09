package runtime

import (
	"bytes"
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func runSrc(t *testing.T, src, entry string, args ...Value) (Value, *VM) {
	t.Helper()
	r := fe.Parse(src)
	if !r.OK() {
		t.Fatalf("parse: %v", r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatalf("compile: %v", errs)
	}
	m = Optimize(m.Tape).wrap(m)
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatalf("load: %v", err)
	}
	v, err := vm.CallBudget(Budget{MaxInstrs: 200000}, entry, args...)
	if err != nil {
		t.Fatalf("call %s: %v", entry, err)
	}
	return v, vm
}

// wrap reattaches an optimized tape to its module.
func (t *Tape) wrap(m *Module) *Module {
	m.Tape = t
	return m
}

func TestArith(t *testing.T) {
	v, _ := runSrc(t, `fn main() { return 1 + 2 * 3 - 4 / 2; }`, "main")
	if v.I != 5 {
		t.Fatalf("got %v", v)
	}
}

func TestFib(t *testing.T) {
	v, _ := runSrc(t, `
fn fib(n: i32) -> i32 {
    if n < 2 { return n; }
    return fib(n - 1) + fib(n - 2);
}
fn main() { return fib(15); }`, "main")
	if v.I != 610 {
		t.Fatalf("got %v", v)
	}
}

func TestClosures(t *testing.T) {
	v, _ := runSrc(t, `
fn adder(n: i32) {
    return |x: i32| -> i32 { return x + n; };
}
fn main() {
    let add5 = adder(5);
    return add5(10) + add5(20);
}`, "main")
	if v.I != 40 {
		t.Fatalf("got %v", v)
	}
}

func TestArraysLoops(t *testing.T) {
	v, _ := runSrc(t, `
fn main() {
    let total = 0;
    for x in [1, 2, 3, 4] { total = total + x; }
    let i = 0;
    while i < 10 { i = i + 1; if i == 5 { continue; } if i == 8 { break; } }
    return total * 100 + i;
}`, "main")
	if v.I != 1008 {
		t.Fatalf("got %v", v)
	}
}

func TestStructMatch(t *testing.T) {
	v, _ := runSrc(t, `
struct Point { x: f32; y: f32; }
fn main() {
    let p = Point { x: 3.0, y: 4.0 };
    let q = match p { Point { x: a, y: b } => a * a + b * b, _ => 0.0 };
    return q;
}`, "main")
	if v.F != 25 {
		t.Fatalf("got %v", v)
	}
}

func TestStringsDefer(t *testing.T) {
	r := fe.Parse(`
fn main() {
    defer { print("second"); }
    print("first");
    return "a" + "b" + str(1 + 1);
}`)
	if !r.OK() {
		t.Fatalf("parse: %v", r.Errors)
	}
	m, _ := Compile(r.File)
	vm := New()
	var buf bytes.Buffer
	vm.SetStdout(&buf)
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	v, err := vm.CallBudget(Budget{MaxInstrs: 100000}, "main")
	if err != nil {
		t.Fatal(err)
	}
	if v.S != "ab2" {
		t.Fatalf("got %v", v)
	}
	if buf.String() != "first\nsecond\n" {
		t.Fatalf("defer order wrong: %q", buf.String())
	}
}

func TestMethodsEnum(t *testing.T) {
	v, _ := runSrc(t, `
enum Dir { North, South }
struct V { n: i32; fn get(self: V) -> i32 { return self.n + 1; } }
fn main() {
    let v = V { n: 41 };
    let d = match Dir::South { Dir::North => 1, _ => 2 };
    return v.get() * 100 + d;
}`, "main")
	if v.I != 4202 {
		t.Fatalf("got %v", v)
	}
}

func TestShortCircuit(t *testing.T) {
	v, _ := runSrc(t, `
fn boom() -> bool { return 1 == 2; }
fn main() {
    let a = true || boom();
    let b = false && boom();
    if a && !b { return 7; }
    return 0;
}`, "main")
	if v.I != 7 {
		t.Fatalf("got %v", v)
	}
}

func TestQuestionOp(t *testing.T) {
	v, _ := runSrc(t, `
fn maybe(x: i32) { if x == 0 { return null; } return x * 2; }
fn main() {
    let a = maybe(21)?;
    return a + 0;
}`, "main")
	if v.I != 42 {
		t.Fatalf("got %v", v)
	}
}

func TestAOTRoundtrip(t *testing.T) {
	r := fe.Parse(`const K: i32 = 6; fn main() { return K * 7; }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, _ := Compile(r.File)
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	img, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	d, err := Decode(img)
	if err != nil {
		t.Fatal(err)
	}
	vm2 := New()
	if err := vm2.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	v, err := vm2.CallBudget(Budget{MaxInstrs: 10000}, "main")
	if err != nil {
		t.Fatal(err)
	}
	if v.I != 42 {
		t.Fatalf("got %v", v)
	}
}

func TestHandlersComplete(t *testing.T) {
	if missing := HasHandler(); len(missing) != 0 {
		t.Fatalf("missing handlers: %v", missing)
	}
}

func TestStackBudget(t *testing.T) {
	r := fe.Parse(`fn boom() -> i32 { return boom(); } fn main() { return boom(); }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, _ := Compile(r.File)
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	_, err := vm.CallBudget(Budget{MaxInstrs: 100000}, "main")
	if err == nil || !strings.Contains(err.Error(), "overflow") {
		t.Fatalf("expected stack overflow, got %v", err)
	}
}

func TestMatchProbes(t *testing.T) {
	cases := map[string]int64{
		`fn main() { return match 1 { 1 => 10, _ => 20 }; }`:                      10,
		`enum D { A, B } fn main() { return match D::B { D::A => 1, _ => 2 }; }`: 2,
		`fn main() { return match [1, 2] { [a, b] => a + b, _ => 0 }; }`:        3,
		`fn main() { let x = 5; return match x { 1 | 2 => 100, _ => 200 }; }`:    200,
	}
	for src, want := range cases {
		v, _ := runSrc(t, src, "main")
		if v.I != want {
			t.Errorf("%s: got %v want %d", src, v, want)
		}
	}
}

func TestQuestionProbe(t *testing.T) {
	v, _ := runSrc(t, `fn main() { let a = 5?; return a + 1; }`, "main")
	if v.I != 6 {
		t.Fatalf("got %v", v)
	}
}
