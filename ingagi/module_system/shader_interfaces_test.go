package module_system

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	shader "github.com/Chubek/JoltFX/ingagi/monkeyshade"
	"testing"
	"testing/fstest"
)

func TestImportedShaderNamespace(t *testing.T) {
	files := fstest.MapFS{
		"main.ing": {Data: []byte(`import gpu; pipeline P {vertex:gpu::S::vertex;fragment:gpu::S::fragment;}`)},
		"gpu.ing": {Data: []byte(`module gpu; const Attrs=1; const inputs=2;
  public shader S {
   interface Attrs {@location(0) position:vec3;}
   interface O {@location(0) uv:vec2;}
   vertex(inputs:Attrs,outputs:O){gl_Position=vec4(inputs.position,1.0);outputs.uv=vec2(0.0);}
   fragment(inputs:O){}
  }`)},
	}
	f, err := (&Loader{FS: files}).Load("main.ing")
	if err != nil {
		t.Fatal(err)
	}
	var pipeline *fe.PipelineDecl
	var s *fe.ShaderDecl
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.ShaderDecl:
			s = d
		case *fe.PipelineDecl:
			pipeline = d
		}
	}
	if s == nil || s.Name != "gpu::S" {
		t.Fatal("shader name not qualified")
	}
	if _, err = shader.ReflectPipeline(f, pipeline); err != nil {
		t.Fatal(err)
	}
	if _, err = shader.CompileGLSL(s, "vertex"); err != nil {
		t.Fatal(err)
	}
}
