package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"testing"
)

func parseTest(t *testing.T, src string) *fe.File {
	t.Helper()
	p := fe.Parse(src)
	if !p.OK() {
		t.Fatal(p.Errors)
	}
	return p.File
}

func TestTypeAnalysis(t *testing.T) {
	for _, tc := range []struct{ src, code string }{
		{`fn f(x: i64) -> i64 { return x + 1; } fn main() -> i64 { return f(2); }`, ""},
		{`fn f() -> i64 { return "wrong"; }`, "E0201"},
		{`fn f() { let x: bool = 42; }`, "E0201"},
		{`fn f(x: i64) {} fn main() { f("wrong"); }`, "E0201"},
		{`fn f() { return 1 + false; }`, "E0202"},
		{`struct Point { x: i64; } fn f() { return Point { y: 1 }; }`, "E0203"},
		{`fn f() { let x = 1; { let x = "shadow"; } return x + 1; }`, ""},
		{`fn f() { return host_fn(1); }`, ""},
	} {
		f := parseTest(t, tc.src)
		ds := CheckType(f, BuildRegistry(f))
		if tc.code == "" {
			if HasErrors(ds) {
				t.Errorf("%s: %v", tc.src, ds)
			}
			continue
		}
		found := false
		for _, d := range ds {
			found = found || d.Code == tc.code
		}
		if !found {
			t.Errorf("%s: want %s, got %v", tc.src, tc.code, ds)
		}
	}
}

func TestLogicAndOverflow(t *testing.T) {
	f := parseTest(t, `fn f() -> i64 { if true { return 1; } } fn g() { return 1; print(2); } fn h() { let x = 9223372036854775807 + 1; let y = 10 / (2 - 2); }`)
	r := BuildRegistry(f)
	logic := CheckLogic(f, r)
	if len(logic) != 2 || logic[0].Code != "E0601" || logic[1].Code != "W0602" {
		t.Fatal(logic)
	}
	overflow := CheckOverflow(f, r)
	if len(overflow) != 2 || overflow[0].Code != "W0701" || overflow[1].Code != "E0702" {
		t.Fatal(overflow)
	}
}

func TestScopeBoundaries(t *testing.T) {
	for _, src := range []string{
		`fn f() { if true { let hidden = 1; } return hidden; }`,
		`fn f() { while false { let hidden = 1; } return hidden; }`,
		`fn f() { for hidden in [1] {} return hidden; }`,
		`fn f() { defer { print(missing); } }`,
	} {
		f := parseTest(t, src)
		ds := CheckScope(f, BuildRegistry(f))
		if !HasErrors(ds) {
			t.Errorf("accepted %s", src)
		}
	}
	f := parseTest(t, `fn f() { print(1); let x = 1; { let x = 2; print(x); } return x; }`)
	if ds := CheckScope(f, BuildRegistry(f)); HasErrors(ds) {
		t.Fatal(ds)
	}
}
