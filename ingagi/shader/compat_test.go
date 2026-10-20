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
