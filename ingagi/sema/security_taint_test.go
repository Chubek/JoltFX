package sema

import (
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func TestSecurityAnalysis(t *testing.T) {
	unsafeSource := `fn read(p: *i64) -> i64 { unsafe { return *p; } }`
	r := fe.Parse(unsafeSource)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	if ds := CheckSecurity(r.File, BuildRegistry(r.File)); len(ds) != 2 || ds[0].Sev != Error || ds[1].Sev != Note {
		t.Fatalf("pointer boundary diagnostics: %v", ds)
	}

	r = fe.Parse(`fn read(p: *i64) -> i64 { return *p; }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	if ds := CheckSecurity(r.File, BuildRegistry(r.File)); len(ds) != 2 || ds[0].Code != "E0901" || ds[1].Code != "E0901" {
		t.Fatalf("unprotected pointer diagnostics: %v", ds)
	}
}

func TestTaintAnalysis(t *testing.T) {
	r := fe.Parse(`
extern "host" { fn host_read(x: i64) -> i64; }
fn identity(x: i64) -> i64 { return x; }
fn main() { let raw = host_read(1); let a = identity(raw); print(a); }
`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	ds := CheckTaint(r.File, BuildRegistry(r.File))
	if len(ds) < 1 {
		t.Fatalf("expected a taint warning for the extern flow and output, got %v", ds)
	}
	for _, d := range ds {
		if d.Code != "W1001" || d.Sev != Warning {
			t.Fatalf("unexpected taint diagnostic: %v", d)
		}
	}

	r = fe.Parse(`fn main() { print(1 + 2); }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	if ds := CheckTaint(r.File, BuildRegistry(r.File)); len(ds) != 0 {
		t.Fatalf("constant flow was marked tainted: %v", ds)
	}
}

func TestTaintFlowRegressions(t *testing.T) {
	for _, src := range []string{
		`fn main() { print(fetch()); } fn fetch() { return host_read(); }`,
		`fn main() { let a = 0; if true { a = host_read(); } else { a = 0; } print(a); }`,
		`fn main() { let a = [0]; a[0] = host_read(); print(a[0]); }`,
		`fn main() { let x = host_read(); let a = [x, print(host_read())]; }`,
		`fn main() { print(fetch()); } fn fetch() { if true { return host_read(); } return 0; }`,
		`fn main() { let x = 0; let y = 0; while true { x = y; y = host_read(); } print(x); }`,
		`fn main() { let x = host_read(); false && ((x = 0) == 0); print(x); }`,
		`fn main() { let x = host_read(); x += (x = 0); print(x); }`,
	} {
		r := fe.Parse(src)
		if !r.OK() {
			t.Fatal(r.Errors)
		}
		if ds := CheckTaint(r.File, BuildRegistry(r.File)); len(ds) == 0 {
			t.Errorf("lost host flow in %s", src)
		}
	}
}

func TestTaintScopesAndDeclarationOrder(t *testing.T) {
	for _, src := range []string{
		`fn read() { return host_read(); } fn main() { let x = 0; { let x = host_read(); } print(x); }`,
		`fn main() { let x = 0; { let x = host_read(); } print(x); } fn read() { return host_read(); }`,
	} {
		r := fe.Parse(src)
		if !r.OK() {
			t.Fatal(r.Errors)
		}
		if ds := CheckTaint(r.File, BuildRegistry(r.File)); len(ds) != 0 {
			t.Errorf("block-local taint leaked: %v", ds)
		}
	}
}
