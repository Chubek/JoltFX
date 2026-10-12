package compiler

import (
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func parseNative(t *testing.T, src string) *fe.File {
	t.Helper()
	r := fe.Parse(src)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	return r.File
}

func TestQBEDiagnostics(t *testing.T) {
	for _, tc := range []struct{ src, message string }{
		{`fn f() -> i64 { return missing; }`, "undefined"},
		{`fn f(x: string) -> i64 { return 0; }`, "unsupported type"},
		{`fn f() -> i64 { return true; }`, "return type"},
		{`fn f(x: i64) -> i64 { return x; } fn main() -> i64 { return f(); }`, "arguments"},
		{`fn f() -> i64 { let x = 1; x = false; return x; }`, "assignment type"},
		{`fn f() -> i64 { let x: i64; return x; }`, "initializer"},
		{`fn f() -> i64 { if true { return 1; } }`, "return"},
		{`fn f() -> i64 { break; return 0; }`, "outside loop"},
		{`fn f() -> i64 { let f = |x: i64| { return x; }; return 0; }`, "unsupported expression"},
		{`component P { x: i64; } fn f() -> i64 { return 0; }`, "unsupported declaration"},
		{`fn f() -> i64 { return 1; } fn f() -> i64 { return 2; }`, "duplicate"},
		{`fn f() -> i64 { if true { let x = 1; } return x; }`, "undefined"},
	} {
		t.Run(tc.message, func(t *testing.T) {
			p, err := EmitQBE(parseNative(t, tc.src))
			if err == nil || p != nil || !strings.Contains(err.Error(), tc.message) {
				t.Fatalf("got program=%v, error=%v; want %q", p, err, tc.message)
			}
		})
	}
}

func TestQBEDeterminismAndControlFlow(t *testing.T) {
	f := parseNative(t, `fn sum(a: i64, b: i64) -> i64 { return a + b; }
        fn main() -> i64 {
            let x = 0;
            while x < 10 { x += 1; if x == 3 { continue; } if x == 8 { break; } }
            return sum(x, 34);
        }`)
	a, err := EmitQBE(f)
	if err != nil {
		t.Fatal(err)
	}
	b, err := EmitQBE(f)
	if err != nil || a.IL != b.IL {
		t.Fatal("non-deterministic QBE output", err)
	}
	for _, text := range []string{"function l", "jnz", "call $ig_fn_0", "alloc8"} {
		if !strings.Contains(a.IL, text) {
			t.Fatalf("missing %q in %s", text, a.IL)
		}
	}
}
