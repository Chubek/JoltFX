package sema

import (
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func TestAvailablePasses(t *testing.T) {
	r := fe.Parse(`fn main() { return 42; }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	if ds := Check(r.File, DefaultOptions()); len(ds) != 0 {
		t.Fatal(ds)
	}
	if ds := Check(r.File, Options{}); len(ds) != 0 {
		t.Fatalf("unexpected diagnostics from complete pass set: %v", ds)
	}
}

func TestDefaultScopeDiagnostic(t *testing.T) {
	r := fe.Parse(`fn main() { return missing; }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	ds := Check(r.File, DefaultOptions())
	if len(ds) != 1 || ds[0].Code != "E0102" {
		t.Fatalf("got %v", ds)
	}
}
