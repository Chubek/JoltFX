package runtime

import (
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// runTape installs a hand-built tape, bypassing Validate, and runs it from pc
// 0. This exercises the defensive checks in the dispatch handlers, which are
// the last line of defense for tapes a host embedding mutated directly.
func runTape(t *testing.T, pool *ConstPool, code []Instr, budget Budget) (Value, error) {
	t.Helper()
	vm := New()
	vm.tape = &Tape{Code: code, Pool: pool, Entry: map[string]int{"main": 0}}
	vm.funcs = map[string]int{"main": 0}
	vm.params = map[string]int{"main": 0}
	if err := vm.frames.push(frame{locals: []Value{}}); err != nil {
		t.Fatal(err)
	}
	return vm.run(0, budget)
}

// huge is the count that previously allocated gigabytes via make([]Value, n).
const huge = 1<<31 - 1

// TestAggregateCountsAreBounded checks that a count operand larger than the
// available operand stack is a runtime error rather than a large allocation.
// This is the regression test for the out-of-memory finding: OpMakeArray and
// friends sized a slice straight from the instruction operand.
func TestAggregateCountsAreBounded(t *testing.T) {
	pool := NewConstPool()
	pool.InternString("T")
	pool.InternString("T::V")
	for _, tc := range []struct {
		name string
		op   Op
		a, b int32
	}{
		{"make_array", OpMakeArray, huge, 0},
		{"make_map", OpMakeMap, huge, 0},
		{"make_struct", OpMakeStruct, huge, 0},
		{"make_enum", OpMakeEnum, huge, 0},
		{"call", OpCall, huge, 0},
		{"call_method", OpCallMethod, huge, 0},
		{"make_closure", OpMakeClosure, huge, 0},
		{"foreach_begin", OpForEachBegin, huge, 0},
		{"bind_with", OpBind, huge, 0},
		{"bind_without", OpBind, 0, huge},
		{"negative_array", OpMakeArray, -1, 0},
		{"overflow_map", OpMakeMap, huge/2 + 1, 0},
	} {
		t.Run(tc.name, func(t *testing.T) {
			code := []Instr{{Op: tc.op, A: tc.a, B: tc.b}, {Op: OpHalt}}
			_, err := runTape(t, pool, code, Budget{MaxInstrs: 1000})
			if err == nil {
				t.Fatalf("%s with count %d was not rejected", tc.name, tc.a)
			}
			if !strings.Contains(err.Error(), "need") && !strings.Contains(err.Error(), "count") {
				t.Fatalf("unexpected diagnostic: %v", err)
			}
		})
	}
}

// TestArrayRepeatIsBounded covers `[x; n]`, whose size does not correspond to a
// stack depth, so it needs its own limit.
func TestArrayRepeatIsBounded(t *testing.T) {
	pool := NewConstPool()
	pool.InternFloat(1)             // the repeated element
	pool.InternFloat(float64(huge)) // the repeat count
	code := []Instr{
		{Op: OpPushFloat, C: 0}, // element
		{Op: OpPushFloat, C: 1}, // count
		{Op: OpMakeArrayRepeat},
		{Op: OpHalt},
	}
	if _, err := runTape(t, pool, code, Budget{MaxInstrs: 1000}); err == nil {
		t.Fatal("array repeat accepted an unbounded count")
	} else if !strings.Contains(err.Error(), "exceeds") {
		t.Fatalf("unexpected diagnostic: %v", err)
	}
}

// TestValidCountsStillWork guards against the new bounds rejecting legitimate
// aggregate construction.
func TestValidCountsStillWork(t *testing.T) {
	for _, tc := range []struct {
		name string
		src  string
		want int64
	}{
		{"array", `fn main() { let a = [1, 2, 3]; return a[1]; }`, 2},
		{"nested array", `fn main() { let a = [[1], [2, 3]]; return a[1][1]; }`, 3},
		{"struct", `struct P { x: i32; } fn main() { let p = P { x: 7 }; return p.x; }`, 7},
		{"enum", `enum S { A, B } fn main() { let s = S::B; return match s { S::A => 0, _ => 5 }; }`, 5},
		{"closure call", `fn main() { let f = |x: i32| { return x + 1; }; return f(41); }`, 42},
		{"method call", `struct V { n: i32; fn get(self: V) -> i32 { return self.n; } } fn main() { return V { n: 9 }.get(); }`, 9},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := fe.Parse(tc.src)
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
			if v, err := vm.CallBudget(Budget{MaxInstrs: 100000}, "main"); err != nil || v.I != tc.want {
				t.Fatalf("returned %v, want %d: %v", v, tc.want, err)
			}
		})
	}
}

// TestPoolAccessorsDoNotPanicUnderHostUse confirms the hardened accessors keep
// a stale pool from panicking a handler.
func TestPoolAccessorsDoNotPanicUnderHostUse(t *testing.T) {
	pool := NewConstPool()
	pool.InternInt(7)
	pool.InternString("hello")
	// Out-of-range reads return zero values.
	if pool.Int(-1) != 0 || pool.Int(1) != 0 || pool.Int(99) != 0 {
		t.Fatal("Int did not bounds-check")
	}
	if pool.String(-1) != "" || pool.String(1) != "" || pool.String(99) != "" {
		t.Fatal("String did not bounds-check")
	}
	if pool.Float(-1) != 0 || pool.Float(0) != 0 || pool.Float(5) != 0 {
		t.Fatal("Float did not bounds-check")
	}
	// In-range reads are unaffected.
	if pool.Int(0) != 7 || pool.String(0) != "hello" {
		t.Fatal("bounds-check changed valid reads")
	}
}

// A tiny instruction budget still stops normal code after handler hardening.
func TestHardenedHandlersRespectBudget(t *testing.T) {
	r := fe.Parse(`fn main() { let a = 1; return a; }`)
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
	// A tiny budget must fail with a budget error, not an allocation fault.
	_, err := vm.CallBudget(Budget{MaxInstrs: 2}, "main")
	if err == nil {
		t.Fatal("expected the instruction budget to stop the run")
	}
	if !strings.Contains(err.Error(), "budget") {
		t.Fatalf("unexpected error: %v", err)
	}
}
