package compiler

import (
	"strings"
	"testing"
)

func TestTargetCatalog(t *testing.T) {
	specs, err := Targets()
	if err != nil {
		t.Fatal(err)
	}
	if len(specs) != 12 {
		t.Fatalf("got %d targets", len(specs))
	}
	for _, spec := range specs {
		if err := spec.RequireArtifact(spec.Artifacts[0]); (err == nil) != (spec.Status == "implemented") {
			t.Fatalf("%s: %v", spec.ID, err)
		}
	}
	q, err := LookupTarget("QBE")
	if err != nil || q.Driver != "qbe" {
		t.Fatalf("%+v: %v", q, err)
	}
	if err := q.RequireArtifact("spirv"); err == nil {
		t.Fatal("accepted unsupported artifact")
	}
	if _, err := LookupTarget("../../QBE"); err == nil {
		t.Fatal("accepted unknown target")
	}
	q.Artifacts[0] = "mutated"
	again, _ := LookupTarget("QBE")
	if again.Artifacts[0] == "mutated" {
		t.Fatal("catalog aliases mutable data")
	}
}

func TestInvalidTargetSpecs(t *testing.T) {
	for _, src := range []string{`)`, `()`, `(target)`, `(target (schema 2))`, `(target (id X) (id Y))`, `(target (unknown X))`, strings.Repeat("(", 130)} {
		if _, err := ParseTargetSpec(src); err == nil {
			t.Fatalf("accepted %q", src)
		}
	}
}
