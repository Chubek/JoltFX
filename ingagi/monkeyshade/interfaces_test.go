package shader

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

const graphicsSource = `shader Draw {
 interface VertexInput { @location(0) position: vec3; @location(1) uv: vec2; }
 interface Varyings { @location(0) uv: vec2; @location(1) @flat material: u32; }
 interface Colors { @location(0) color: vec4; }
 @set(1) @binding(2) uniform Camera { matrix: mat4; } camera;
 vertex(inputs: VertexInput, outputs: Varyings) {
  gl_Position = camera.matrix * vec4(inputs.position, 1.0);
  outputs.uv = inputs.uv;
  outputs.material = uint(1);
 }
 fragment(inputs: Varyings, outputs: Colors) {
  outputs.color = vec4(inputs.uv, float(inputs.material), 1.0);
 }
}`

func TestGraphicsInterfaces(t *testing.T) {
	s := parseShaderTest(t, graphicsSource)
	if !s.Structs[0].Interface {
		t.Fatal("parser erased interface distinction")
	}
	for _, stage := range []string{"vertex", "fragment"} {
		glsl, err := CompileGLSL(s, stage)
		if err != nil {
			t.Fatal(err)
		}
		if !strings.Contains(glsl, "layout(location = 0)") || strings.Contains(glsl, "struct VertexInput") || strings.Contains(glsl, "inputs.") || strings.Contains(glsl, "outputs.") {
			t.Fatal(glsl)
		}
		reflected, err := Reflect(s, stage)
		if err != nil {
			t.Fatal(err)
		}
		if len(reflected.Inputs) != 2 || len(reflected.Outputs) == 0 || len(reflected.Resources) != 1 {
			t.Fatalf("%+v", reflected)
		}
	}
	linked, err := LinkGraphics(s, s)
	if err != nil {
		t.Fatal(err)
	}
	if len(linked.Stages) != 2 || len(linked.Resources) != 1 {
		t.Fatalf("%+v", linked)
	}
}

func TestComputePipelineReflection(t *testing.T) {
	parsed := fe.Parse(`shader S {compute(local_size:8){}} pipeline C {compute:S::compute;compute {groups:2;}}`)
	if !parsed.OK() {
		t.Fatal(parsed.Errors)
	}
	r, err := ReflectPipeline(parsed.File, parsed.File.Decls[1].(*fe.PipelineDecl))
	if err != nil || len(r.Stages) != 1 || r.Stages[0].LocalSize[0] != 8 || r.FixedFunctionStateValidated {
		t.Fatalf("%+v: %v", r, err)
	}
}

func TestInterfaceDiagnostics(t *testing.T) {
	for _, source := range []string{
		`shader S {interface I {x:vec3;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) x:vec3; @location(0) y:float;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) x:mat4; @location(3) y:float;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(63) x:mat2;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) x:bool;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) x:mat4;} fragment(outputs:I){}}`,
		`shader S {interface I {@location(0) x:int;} vertex(outputs:I){}}`,
		`shader S {interface I {@location(0) @flat x:vec3;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) @unknown x:vec3;} vertex(inputs:I){}}`,
		`shader S {vertex(inputs:Missing){}}`,
		`shader S {struct I {x:vec3;} vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) x:vec3;} vertex(inputs:I){let inputs:vec3=vec3(0.0);}}`,
		`shader S {interface I {@location(0) x:vec3;} vertex(inputs:I){gl_Position=vec4(inputs.missing,1.0);}}`,
		`shader S {interface I {@location(0) x:vec3;} compute(inputs:I){}}`,
		`shader S {interface I {@location(0) x:vec3;} uniform U {x:vec3;} inputs; vertex(inputs:I){}}`,
		`shader S {interface I {@location(0) x:vec3;} vertex(inputs:I){inputs.x=vec3(0.0);}}`,
		`shader S {interface I {@location(0) @flat @smooth x:vec3;} fragment(inputs:I){}}`,
		`shader S {interface I {@location(0) @centroid @sample x:vec3;} fragment(inputs:I){}}`,
	} {
		p := fe.Parse(source)
		if !p.OK() {
			t.Fatalf("parse: %s: %v", source, p.Errors)
		}
		s := p.File.Decls[0].(*fe.ShaderDecl)
		stage := s.Stages[0].Kind
		if _, err := CompileGLSL(s, stage); err == nil {
			t.Fatalf("accepted: %s", source)
		}
	}
	for _, source := range []string{`shader S {vertex(inputs:A,inputs:B){}}`, `pipeline P {vertex:A::vertex;vertex:B::vertex;}`} {
		if fe.Parse(source).OK() {
			t.Fatalf("duplicate option/reference accepted: %s", source)
		}
	}
}

func TestMatrixArrayInterfaces(t *testing.T) {
	s := parseShaderTest(t, `shader S {
	 interface A {@location(0) transform:mat4; @location(4) coordinates:[vec2;2];}
	 interface B {@location(0) @noperspective @centroid uv:vec2;}
	 vertex(inputs:A,outputs:B){gl_Position=inputs.transform*vec4(0.0);outputs.uv=inputs.coordinates[1];}
	 fragment(inputs:B){}
	}`)
	if _, err := LinkGraphics(s, s); err != nil {
		t.Fatal(err)
	}
	r, err := Reflect(s, "vertex")
	if err != nil || r.Inputs[0].Locations != 4 || r.Inputs[1].Locations != 2 {
		t.Fatalf("%+v: %v", r, err)
	}
	if compiler, err := exec.LookPath("glslangValidator"); err == nil {
		for _, stage := range []string{"vertex", "fragment"} {
			if _, err := CompileSPIRV(context.Background(), s, stage, compiler); err != nil {
				t.Fatal(err)
			}
		}
	}
}

func TestResourceLinkContract(t *testing.T) {
	vertex := parseShaderTest(t, `shader V {@binding(2) read storage buffer VData {v:vec4;} data; vertex{gl_Position=vec4(0.0);}}`)
	fragment := parseShaderTest(t, `shader F {@binding(2) write storage buffer FData {different:vec4;} other;fragment{}}`)
	r, err := LinkGraphics(vertex, fragment)
	if err != nil || r.Resources[0].Access != "read_write" || len(r.Resources[0].Stages) != 2 {
		t.Fatalf("%+v: %v", r, err)
	}
	for _, declaration := range []string{`@binding(2) uniform FData {v:vec4;} other;`, `@binding(2) storage buffer FData {v:ivec4;} other;`, `@binding(2) storage_image texture2d image:write;`} {
		f := parseShaderTest(t, `shader F {`+declaration+`fragment{}}`)
		if _, err := LinkGraphics(vertex, f); err == nil {
			t.Fatal("incompatible resource link accepted")
		}
	}
}

func TestGraphicsLinkFailures(t *testing.T) {
	vertex := parseShaderTest(t, `shader V {interface O {@location(0) uv:vec2;} vertex(outputs:O){outputs.uv=vec2(0.0);gl_Position=vec4(0.0);}}`)
	for _, fields := range []string{`@location(0) uv:vec3;`, `@location(1) uv:vec2;`, `@location(0) @flat uv:vec2;`} {
		fragment := parseShaderTest(t, `shader F {interface I {`+fields+`} fragment(inputs:I){}}`)
		if _, err := LinkGraphics(vertex, fragment); err == nil {
			t.Fatal("accepted mismatched link")
		}
	}
	fragment := parseShaderTest(t, `shader F {interface I {@location(0) differentlyNamed:vec2;} fragment(inputs:I){}}`)
	if _, err := LinkGraphics(vertex, fragment); err != nil {
		t.Fatal("link must use locations, not field names:", err)
	}
	if _, err := LinkGraphics(nil, fragment); err == nil {
		t.Fatal("nil vertex shader")
	}
}

func TestRealGraphicsShaderLink(t *testing.T) {
	compiler, err := exec.LookPath("glslangValidator")
	if err != nil {
		t.Skip(err)
	}
	validator, err := exec.LookPath("spirv-val")
	if err != nil {
		t.Skip(err)
	}
	s := parseShaderTest(t, graphicsSource)
	dir := t.TempDir()
	var paths []string
	for _, stage := range []string{"vertex", "fragment"} {
		data, err := CompileSPIRV(context.Background(), s, stage, compiler)
		if err != nil {
			t.Fatal(err)
		}
		spv := filepath.Join(dir, stage+".spv")
		if err = os.WriteFile(spv, data, 0600); err != nil {
			t.Fatal(err)
		}
		if out, err := exec.Command(validator, "--target-env", "vulkan1.1", spv).CombinedOutput(); err != nil {
			t.Fatalf("%s: %v", out, err)
		}
		glsl, _ := CompileGLSL(s, stage)
		file := filepath.Join(dir, "draw."+stageNames[stage])
		if err = os.WriteFile(file, []byte(glsl), 0600); err != nil {
			t.Fatal(err)
		}
		paths = append(paths, file)
	}
	args := append([]string{"-V", "--target-env", "vulkan1.1", "-l", "-o", filepath.Join(dir, "linked.spv")}, paths...)
	if out, err := exec.Command(compiler, args...).CombinedOutput(); err != nil {
		t.Fatalf("real stage link: %s: %v", out, err)
	}
}
