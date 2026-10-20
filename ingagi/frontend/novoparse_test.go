//go:build novoparse && cgo

package frontend

import (
	"bytes"
	"encoding/json"
	"testing"
)

func TestNovoParseConcrete(t *testing.T) {
	r := ParseConcrete(`module demo; fn main() { return 1 + 2 * 3; }`)
	if len(r.Errors) != 0 {
		t.Fatal(r.Errors)
	}
	if !json.Valid(r.Tree) || !bytes.Contains(r.Tree, []byte("CompilationUnit")) || !bytes.Contains(r.Tree, []byte("ReturnStmt")) {
		t.Fatalf("missing native syntax tree: %s", r.Tree)
	}
}

func TestNovoParseRejectsInvalidInput(t *testing.T) {
	for _, src := range []string{`fn main( { return 42; }`, `fn main() { return 42; } @`, "fn main() {}\x00"} {
		r := ParseConcrete(src)
		if len(r.Errors) == 0 || len(r.Tree) != 0 {
			t.Fatalf("accepted invalid input %q", src)
		}
		if r.Errors[0].Span.Line < 1 {
			t.Fatalf("missing source position: %v", r.Errors)
		}
	}
}

func TestNovoParseShaderInterfaces(t *testing.T) {
	source := `shader S {interface Inputs {@location(0) position:vec3;} vertex(inputs:Inputs) {gl_Position=vec4(inputs.position,1.0);} compute{}}
	pipeline C {compute:S::compute;compute {groups:1;}}`
	r := ParseConcrete(source)
	if len(r.Errors) != 0 || !bytes.Contains(r.Tree, []byte("ShaderInterface")) {
		t.Fatalf("%s: %v", r.Tree, r.Errors)
	}
	parsed := Parse(source)
	if !parsed.OK() {
		t.Fatal(parsed.Errors)
	}
	p := parsed.File.Decls[1].(*PipelineDecl)
	if p.Stages["compute"].Last() != "compute" || p.Compute["groups"] == nil {
		t.Fatal("compute reference/state ambiguity")
	}
}
