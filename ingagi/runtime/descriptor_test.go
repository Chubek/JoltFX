package runtime

import (
	"bytes"
	"strings"
	"testing"
)

func TestDescriptorReferences(t *testing.T) {
	for _, tc := range []struct {
		name   string
		change func(*Module)
		want   string
	}{
		{"method", func(m *Module) { m.Types["P"].Methods["bad"] = "missing" }, "method"},
		{"kind", func(m *Module) { m.Types["P"].Kind = "bogus" }, "kind"},
		{"fields", func(m *Module) { m.Types["P"].Fields = append(m.Types["P"].Fields, TypeField{Name: "x"}) }, "duplicate"},
		{"variants", func(m *Module) { m.Types["P"].Variants = []string{"Wrong"} }, "variants"},
		{"hook entry", func(m *Module) { m.Hooks[0].Entry = "missing" }, "entry"},
		{"hook system", func(m *Module) { m.Hooks[0].System = "missing" }, "system"},
		{"hook kind", func(m *Module) { m.Hooks[0].Kind = "bogus" }, "hook kind"},
		{"duplicate hook", func(m *Module) { m.Hooks = append(m.Hooks, m.Hooks[0]) }, "duplicate"},
		{"prefab", func(m *Module) { delete(m.Tape.Entry, "prefab::Actor"); delete(m.Params, "prefab::Actor") }, "prefab"},
		{"arity", func(m *Module) { m.Params["missing"] = 0 }, "entry"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			m := compileMain(t, `struct P { x: i64; } entity Actor {} system S { update(dt: f32) {} } fn main() { return 42; }`)
			if err := Validate(m); err != nil {
				t.Fatal(err)
			}
			tc.change(m)
			if err := Validate(m); err == nil || !strings.Contains(err.Error(), tc.want) {
				t.Fatalf("got %v, want %s", err, tc.want)
			}
		})
	}
}

func TestImageRestoresNestedBuiltin(t *testing.T) {
	m := compileMain(t, `const funcs = [len]; fn main() { return funcs[0]([1, 2, 3]); }`)
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	image, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	d, err := Decode(image)
	if err != nil {
		t.Fatal(err)
	}
	restored := New()
	if err := restored.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	v, err := restored.Call("main")
	if err != nil || v.I != 3 {
		t.Fatalf("got %v, %v", v, err)
	}
}

func TestDecodedInvalidValuesPreservePreviousModule(t *testing.T) {
	vm := New()
	if err := vm.LoadModule(compileMain(t, `fn main() { return 42; }`)); err != nil {
		t.Fatal(err)
	}
	for _, value := range []Value{
		{K: KArray, O: (*ArrayObj)(nil)},
		{K: KClosure, O: &ClosureObj{Name: "missing"}},
		{K: KBuiltin, O: &BuiltinObj{Name: "missing"}},
		{K: KEnum, O: &EnumObj{Type: "missing", Variant: "X"}},
	} {
		d := &Decoded{Module: compileMain(t, `fn main() { return 0; }`), Globals: map[string]Value{"bad": value}}
		if err := vm.LoadDecoded(d); err == nil {
			t.Fatal("accepted invalid value")
		}
		if v, err := vm.Call("main"); err != nil || v.I != 42 {
			t.Fatalf("previous state lost: %v %v", v, err)
		}
	}
}

func TestImageHostBindingsArePerVM(t *testing.T) {
	source := New()
	source.RegisterHostFn("answer", func(*VM, []Value) (Value, error) { return IntValue(1), nil })
	if err := source.LoadModule(compileMain(t, `const callbacks = [answer]; fn main() { return callbacks[0](); }`)); err != nil {
		t.Fatal(err)
	}
	image, err := source.Encode()
	if err != nil {
		t.Fatal(err)
	}
	d, err := Decode(image)
	if err != nil {
		t.Fatal(err)
	}
	a, b := New(), New()
	a.RegisterHostFn("answer", func(*VM, []Value) (Value, error) { return IntValue(11), nil })
	b.RegisterHostFn("answer", func(*VM, []Value) (Value, error) { return IntValue(22), nil })
	if err := a.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	if err := b.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	for _, check := range []struct {
		vm   *VM
		want int64
	}{{a, 11}, {b, 22}, {a, 11}} {
		v, err := check.vm.Call("main")
		if err != nil || v.I != check.want {
			t.Fatalf("got %v, %v; want %d", v, err, check.want)
		}
	}
	if d.Globals["callbacks"].O.(*ArrayObj).Elems[0].O.(*BuiltinObj).Fn != nil {
		t.Fatal("loading rewrote decoded callback")
	}
}

func TestImageRejectsCyclesAndUnknownDefaults(t *testing.T) {
	m := compileMain(t, `struct P { x: i64; } fn main() { return 1; }`)
	p := &ArrayObj{}
	v := Value{K: KArray, O: p}
	p.Elems = []Value{v}
	for _, d := range []*Decoded{
		{Module: m, Globals: map[string]Value{"cycle": v}},
		{Module: m, Defs: map[string]map[string]Value{"Unknown": {"x": IntValue(1)}}},
		{Module: m, Defs: map[string]map[string]Value{"P": {"wrong": IntValue(1)}}},
	} {
		if err := New().LoadDecoded(d); err == nil {
			t.Fatal("accepted invalid snapshot")
		}
	}
}

func TestImageRejectsDuplicateAggregateKeys(t *testing.T) {
	for _, tag := range []int{vMap, vStruct} {
		w := &aotWriter{}
		w.b.WriteByte(byte(tag))
		if tag == vStruct {
			w.str("P")
		}
		w.u64(2)
		for i := 0; i < 2; i++ {
			w.str("x")
			if err := w.writeValue(IntValue(int64(i))); err != nil {
				t.Fatal(err)
			}
		}
		r := &aotReader{b: bytes.NewReader(w.b.Bytes())}
		if _, err := r.readValue(); err == nil || !strings.Contains(err.Error(), "duplicate") {
			t.Fatalf("got %v", err)
		}
	}
}
