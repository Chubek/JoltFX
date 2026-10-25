package shader

import (
	"context"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	monkeyshade "github.com/Chubek/JoltFX/ingagi/monkeyshade"
	"testing"
)

func TestCompatibility(t *testing.T) {
	source := `shader Demo { compute(local_size: 8) {} }`
	p := fe.Parse(source)
	if !p.OK() {
		t.Fatal(p.Errors)
	}
	s := p.File.Decls[0].(*fe.ShaderDecl)
	a, err := CompileGLSL(s, "compute")
	if err != nil {
		t.Fatal(err)
	}
	b, err := monkeyshade.CompileGLSL(s, "compute")
	if err != nil || a != b {
		t.Fatal("compatibility API differs")
	}
	if _, err := CompileArtifact(context.Background(), s, "compute", "invalid", ToolOptions{}); err == nil {
		t.Fatal("invalid artifact")
	}
}

func TestFilamentSourceCompatibility(t *testing.T) {
	source := `shader Demo(target:"filament",shading_model:"unlit") {fragment {prepareMaterial(material);material.baseColor=vec4(1.0);}}`
	s, err := Parse(source, "")
	if err != nil || !IsFilament(s) {
		t.Fatalf("%v", err)
	}
	text, err := CompileFilament(s)
	if err != nil {
		t.Fatal(err)
	}
	data, err := CompileSource(context.Background(), source, "", "material", "filament", ToolOptions{})
	if err != nil || string(data) != text {
		t.Fatalf("source/API mismatch: %v", err)
	}
	r, err := ReflectFilament(s)
	if err != nil || r.ShadingModel != "unlit" {
		t.Fatalf("reflection: %+v %v", r, err)
	}
}
