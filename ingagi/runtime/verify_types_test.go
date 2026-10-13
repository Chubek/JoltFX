package runtime

import (
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// compileSrc parses and compiles src, failing the test on errors.
func compileSrc(t *testing.T, src string) *Module {
	t.Helper()
	r := fe.Parse(src)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	return m
}

// mustValidate requires Validate to accept m.
func mustValidate(t *testing.T, m *Module) {
	t.Helper()
	if err := Validate(m); err != nil {
		t.Fatalf("Validate rejected a good module: %v", err)
	}
}

// mustReject requires Validate to reject m with want in the diagnostic.
func mustReject(t *testing.T, m *Module, want string) {
	t.Helper()
	err := Validate(m)
	if err == nil || !strings.Contains(err.Error(), want) {
		t.Fatalf("got %v, want %q", err, want)
	}
}

// callMain loads m and calls main under a test budget.
func callMain(t *testing.T, m *Module) (Value, error) {
	t.Helper()
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	return vm.CallBudget(Budget{MaxInstrs: 100000}, "main")
}

func TestEnumPayloadSignaturesCompiled(t *testing.T) {
	m := compileSrc(t, `enum Opt { None, Some(i64), Pair(i64, string) } fn main() { return 0; }`)
	ti := m.Types["Opt"]
	if ti == nil || ti.Kind != "enum" {
		t.Fatal("missing enum descriptor")
	}
	for variant, want := range map[string]int{"None": 0, "Some": 1, "Pair": 2} {
		if got, ok := ti.EnumArity[variant]; !ok || got != want {
			t.Fatalf("variant %s arity = %d, %v; want %d", variant, got, ok, want)
		}
	}
	mustValidate(t, m)
}

func TestEnumPayloadCallArity(t *testing.T) {
	for _, tc := range []struct {
		name string
		src  string
		want int64
	}{
		{"unit bare", `enum E { A, B } fn main() { let x = E::A; return match x { E::A => 1, _ => 2 }; }`, 1},
		{"unit call", `enum E { A, B } fn main() { let x = E::B(); return match x { E::B => 3, _ => 4 }; }`, 3},
		{"payload", `enum E { A, B(i64) } fn main() { let x = E::B(41); return 7; }`, 7},
		{"two payloads", `enum E { A, P(i64, string) } fn main() { let x = E::P(1, "s"); return 9; }`, 9},
	} {
		t.Run(tc.name, func(t *testing.T) {
			m := compileSrc(t, tc.src)
			mustValidate(t, m)
			v, err := callMain(t, m)
			if err != nil || v.I != tc.want {
				t.Fatalf("got %v, %v; want %d", v, err, tc.want)
			}
		})
	}
	// Static arity mismatches are load-time errors, not runtime traps.
	for _, src := range []string{
		`enum E { A, B(i64) } fn main() { return E::B(1, 2); }`,
		`enum E { A, B(i64) } fn main() { return E::B(); }`,
		`enum E { A(i64) } fn main() { return E::A(1, 2); }`,
	} {
		m := compileSrc(t, src)
		mustReject(t, m, "payload")
		if err := New().LoadModule(m); err == nil || !strings.Contains(err.Error(), "payload") {
			t.Fatalf("LoadModule accepted %q: %v", src, err)
		}
	}
	// A bare reference to a payload variant has no value to load.
	m := compileSrc(t, `enum E { A, B(i64) } fn main() { let x = E::B; return 1; }`)
	mustValidate(t, m) // the tape is well-formed; the name is simply unbound
	if _, err := callMain(t, m); err == nil || !strings.Contains(err.Error(), "undefined name") {
		t.Fatalf("bare payload variant did not fail: %v", err)
	}
}

func TestOpcodeDescriptorReferences(t *testing.T) {
	newModule := func(code []Instr, pool *ConstPool) *Module {
		return &Module{
			Tape:      &Tape{Code: code, Pool: pool, Entry: map[string]int{"main": 0}},
			Types:     map[string]*TypeInfo{},
			Entities:  map[string]*fe.EntityDecl{},
			Params:    map[string]int{"main": 0},
			Resources: map[string]Value{},
		}
	}
	t.Run("closure missing entry", func(t *testing.T) {
		pool := NewConstPool()
		pool.InternString("missing")
		m := newModule([]Instr{{Op: OpMakeClosure, A: 0, C: 0}, {Op: OpHalt}}, pool)
		mustReject(t, m, "missing entry")
	})
	t.Run("closure empty name", func(t *testing.T) {
		pool := NewConstPool()
		pool.InternString("")
		m := newModule([]Instr{{Op: OpMakeClosure, A: 0, C: 0}, {Op: OpHalt}}, pool)
		mustReject(t, m, "empty")
	})
	t.Run("enum unknown type", func(t *testing.T) {
		pool := NewConstPool()
		pool.InternString("Missing::V")
		m := newModule([]Instr{{Op: OpMakeEnum, A: 0, C: 0}, {Op: OpHalt}}, pool)
		mustReject(t, m, "unknown enum")
	})
	t.Run("enum unknown variant", func(t *testing.T) {
		m := compileSrc(t, `enum E { A } fn main() { return 0; }`)
		m.Tape.Code[0] = Instr{Op: OpMakeEnum, A: 0, C: m.Tape.Pool.InternString("E::B")}
		mustReject(t, m, "unknown variant")
	})
	t.Run("enum malformed reference", func(t *testing.T) {
		pool := NewConstPool()
		pool.InternString("NoSeparator")
		m := newModule([]Instr{{Op: OpMakeEnum, A: 0, C: 0}, {Op: OpHalt}}, pool)
		mustReject(t, m, "malformed")
	})
	t.Run("enum arity mismatch", func(t *testing.T) {
		m := compileSrc(t, `enum E { A, B(i64) } fn main() { return 0; }`)
		m.Tape.Code[0] = Instr{Op: OpMakeEnum, A: 3, C: m.Tape.Pool.InternString("E::B")}
		mustReject(t, m, "payload")
	})
	t.Run("struct empty type", func(t *testing.T) {
		pool := NewConstPool()
		pool.InternString("")
		m := newModule([]Instr{{Op: OpMakeStruct, A: 0, C: 0}, {Op: OpHalt}}, pool)
		mustReject(t, m, "empty")
	})
	t.Run("struct enum type", func(t *testing.T) {
		m := compileSrc(t, `enum E { A } fn main() { return 0; }`)
		m.Tape.Code[0] = Instr{Op: OpMakeStruct, A: 0, C: m.Tape.Pool.InternString("E")}
		mustReject(t, m, "not a struct")
	})
	t.Run("method empty name", func(t *testing.T) {
		pool := NewConstPool()
		pool.InternString("")
		m := newModule([]Instr{{Op: OpCallMethod, A: 0, C: 0}, {Op: OpHalt}}, pool)
		mustReject(t, m, "method name is empty")
	})
}

func TestUnusedOperandsMustBeZero(t *testing.T) {
	pool := NewConstPool()
	pool.InternInt(1)
	pool.InternFloat(1)
	pool.InternString("x")
	pool.InternString("")
	cases := []struct {
		name string
		in   Instr
		want string
	}{
		{"add A", Instr{Op: OpAdd, A: 1}, "unused"},
		{"push int A", Instr{Op: OpPushInt, A: 1, C: 0}, "unused"},
		{"push string A", Instr{Op: OpPushString, A: 1, C: 0}, "unused"},
		{"push string B", Instr{Op: OpPushString, B: 1, C: 0}, "unused"},
		{"jump B", Instr{Op: OpJump, A: 1, B: 1}, "unused"},
		{"load local B", Instr{Op: OpLoadLocal, A: 0, B: 2}, "unused"},
		{"make array B", Instr{Op: OpMakeArray, A: 0, B: 1}, "unused"},
		{"call B", Instr{Op: OpCall, A: 0, B: 1, C: 1}, "unused"},
		{"observe B", Instr{Op: OpObserve, A: 0, B: 1, C: 0}, "unused"},
		{"bind C", Instr{Op: OpBind, A: 0, B: 0, C: 1}, "unused"},
		{"foreach B", Instr{Op: OpForEachBegin, A: 0, B: 1}, "unused"},
		{"return B", Instr{Op: OpReturn, A: 0, B: 1}, "unused"},
		{"halt A", Instr{Op: OpHalt, A: 1}, "unused"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			m := &Module{Tape: &Tape{Code: []Instr{tc.in, {Op: OpHalt}}, Pool: pool}, Params: map[string]int{}}
			mustReject(t, m, tc.want)
		})
	}
	// The compiler's own tapes keep unused operands at zero.
	mustValidate(t, compileSrc(t, `fn main() { let a = [1, 2]; return a[0]; }`))
}

func TestEnumDescriptorConsistency(t *testing.T) {
	base := func() *Module {
		return compileSrc(t, `enum E { A, B(i64) } fn main() { return 0; }`)
	}
	t.Run("missing signature", func(t *testing.T) {
		m := base()
		delete(m.Types["E"].EnumArity, "B")
		mustReject(t, m, "payload signature")
	})
	t.Run("negative arity", func(t *testing.T) {
		m := base()
		m.Types["E"].EnumArity["B"] = -1
		mustReject(t, m, "negative arity")
	})
	t.Run("non-enum with signatures", func(t *testing.T) {
		m := compileSrc(t, `struct P { x: i64; } fn main() { return 0; }`)
		m.Types["P"].EnumArity = map[string]int{"x": 1}
		mustReject(t, m, "payload signatures")
	})
	t.Run("v1 unknown arity still loads", func(t *testing.T) {
		m := base()
		m.Types["E"].EnumArity = nil // as decoded from a v1 image
		mustValidate(t, m)
	})
}

func TestSnapshotPreservesEnumSignatures(t *testing.T) {
	m := compileSrc(t, `enum E { A, B(i64) } fn main() { let x = E::B(41); return 7; }`)
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
	if got := d.Module.Types["E"].EnumArity["B"]; got != 1 {
		t.Fatalf("round-trip arity = %d, want 1", got)
	}
	restored := New()
	if err := restored.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	if v, err := restored.Call("main"); err != nil || v.I != 7 {
		t.Fatalf("got %v, %v", v, err)
	}
	// A snapshot whose payload disagrees with the signature is rejected.
	d2, err := Decode(img)
	if err != nil {
		t.Fatal(err)
	}
	d2.Globals["evil"] = Value{K: KEnum, O: &EnumObj{Type: "E", Variant: "B", Payload: []Value{IntValue(1), IntValue(2)}}}
	if err := New().LoadDecoded(d2); err == nil || !strings.Contains(err.Error(), "payload") {
		t.Fatalf("accepted a payload of the wrong length: %v", err)
	}
	// Unknown variants and types in snapshots are still rejected.
	d3, err := Decode(img)
	if err != nil {
		t.Fatal(err)
	}
	d3.Globals["evil"] = Value{K: KEnum, O: &EnumObj{Type: "E", Variant: "Missing"}}
	if err := New().LoadDecoded(d3); err == nil {
		t.Fatal("accepted an unknown variant")
	}
}

// TestDecodeAcceptsV1Images builds a v1-format image (no arity table) by hand
// and requires the v2 decoder to load it with unknown signatures.
func TestDecodeAcceptsV1Images(t *testing.T) {
	var w aotWriter
	w.b.WriteString(aotMagic)
	w.u64(1) // v1
	w.u64(0)
	w.u64(0)
	w.strs(nil)
	w.u64(1) // one Halt
	w.b.WriteByte(byte(OpHalt))
	w.i64(0)
	w.i64(0)
	w.i64(0)
	w.u64(1) // entry main
	w.str("main")
	w.u64(0)
	w.u64(0)
	w.u64(0) // globals
	w.u64(1) // one type: enum E with variant A, v1 layout (no arity)
	w.strs([]string{"E", "enum"})
	w.strs(nil)
	w.strs([]string{"A"})
	w.u64(0) // methods
	w.u64(0) // defaults
	w.u64(0) // hooks
	for i := 0; i < 5; i++ {
		w.strs(nil)
	}
	if w.err != nil {
		t.Fatal(w.err)
	}
	d, err := Decode(w.b.Bytes())
	if err != nil {
		t.Fatalf("v1 image rejected: %v", err)
	}
	if ti := d.Module.Types["E"]; ti == nil || ti.EnumArity != nil {
		t.Fatalf("v1 enum arity should be unknown (nil), got %v", ti)
	}
	vm := New()
	if err := vm.LoadDecoded(d); err != nil {
		t.Fatalf("v1 image did not install: %v", err)
	}
}
