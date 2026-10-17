package shader

import (
	"context"
	"encoding/binary"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

const computeTest = `shader Scale {
 @set(0) @binding(0) storage buffer Data { value: vec4; } data;
 const Factor: float = 2.0;
 fn scale(x: vec4) -> vec4 { return x * Factor; }
 compute(local_size: 1) { data.value = scale(data.value); }
}`

func parseShaderTest(t *testing.T, src string) *fe.ShaderDecl {
	t.Helper()
	p := fe.Parse(src)
	if !p.OK() {
		t.Fatal(p.Errors)
	}
	return p.File.Decls[0].(*fe.ShaderDecl)
}

func TestCompileShader(t *testing.T) {
	s := parseShaderTest(t, computeTest)
	glsl, err := CompileGLSL(s, "compute")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(glsl, "layout(local_size_x = 1") || !strings.Contains(glsl, "void main()") {
		t.Fatal(glsl)
	}
	if _, err := CompileGLSL(s, "fragment"); err == nil {
		t.Fatal("accepted missing stage")
	}
	bad := parseShaderTest(t, `shader Bad { compute { defer { print(1); } } }`)
	if _, err := CompileGLSL(bad, "compute"); err == nil {
		t.Fatal("silently omitted defer")
	}
}

func TestBufferArrays(t *testing.T) {
	s := parseShaderTest(t, `shader Array { storage buffer Data { values: [vec4; 256]; } data; compute { data.values[0] = vec4(1.0); } }`)
	glsl, err := CompileGLSL(s, "compute")
	if err != nil || !strings.Contains(glsl, "vec4 values[256];") {
		t.Fatalf("%s: %v", glsl, err)
	}
	if compiler, err := exec.LookPath("glslangValidator"); err == nil {
		if _, err := CompileSPIRV(context.Background(), s, "compute", compiler); err != nil {
			t.Fatal(err)
		}
		st := parseShaderTest(t, `shader StructArray { struct Item { values: [vec4; 2]; } storage buffer Data { item: Item; } data; compute { data.item.values[1] = vec4(2.0); } }`)
		if _, err := CompileSPIRV(context.Background(), st, "compute", compiler); err != nil {
			t.Fatal(err)
		}
	}
	for _, typ := range []string{"[vec4;0]", "[vec4;65537]", "[vec4;2+2]", "[void;4]"} {
		bad := parseShaderTest(t, `shader Bad { storage buffer Data { values: `+typ+`; } data; compute {} }`)
		if _, err := CompileGLSL(bad, "compute"); err == nil {
			t.Fatalf("accepted %s", typ)
		}
	}
}

func TestSPIRVValidation(t *testing.T) {
	compiler, err := exec.LookPath("glslangValidator")
	if err != nil {
		t.Skip("glslangValidator unavailable")
	}
	validator, err := exec.LookPath("spirv-val")
	if err != nil {
		t.Skip("spirv-val unavailable")
	}
	for _, source := range []string{computeTest, `shader Vertex { vertex { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); } }`, `shader Depth { fragment { gl_FragDepth = 0.5; } }`} {
		s := parseShaderTest(t, source)
		stage := s.Stages[0].Kind
		data, err := CompileSPIRV(context.Background(), s, stage, compiler)
		if err != nil {
			t.Fatal(err)
		}
		if len(data) < 20 || binary.LittleEndian.Uint32(data) != 0x07230203 {
			t.Fatal("invalid SPIR-V header")
		}
		file := filepath.Join(t.TempDir(), "shader.spv")
		if err := os.WriteFile(file, data, 0600); err != nil {
			t.Fatal(err)
		}
		if out, err := exec.Command(validator, "--target-env", "vulkan1.1", file).CombinedOutput(); err != nil {
			t.Fatalf("%s: %v", out, err)
		}
	}
}

func TestShaderRejectsBindingsAndOptions(t *testing.T) {
	for _, source := range []string{
		`shader Bad { @binding(0) uniform A { x: float; } a; @binding(0) uniform B { x: float; } b; compute {} }`,
		`shader Bad { compute(local_size: 0) {} }`,
		`shader Bad { compute(unknown: 1) {} }`,
		`shader Bad { compute { let x = 1; } }`,
	} {
		if _, err := CompileGLSL(parseShaderTest(t, source), "compute"); err == nil {
			t.Errorf("accepted %s", source)
		}
	}
}

func TestGeometryAndTessellationStages(t *testing.T) {
	for _, tc := range []struct {
		src, stage, want string
	}{
		{`shader G { geometry(input: points, output: line_strip, max_vertices: 4) { EmitVertex(); EndPrimitive(); } }`, "geometry", "layout(line_strip, max_vertices = 4) out;"},
		{`shader T { tess_control(vertices: 4) { } }`, "tess_control", "layout(vertices = 4) out;"},
		{`shader E { tess_evaluation(primitive: quads, spacing: equal_spacing, ordering: ccw, point_mode: true) { } }`, "tess_evaluation", "layout(equal_spacing, ccw, point_mode, quads) in;"},
	} {
		glsl, err := CompileGLSL(parseShaderTest(t, tc.src), tc.stage)
		if err != nil {
			t.Fatalf("%s: %v", tc.stage, err)
		}
		if !strings.Contains(glsl, tc.want) {
			t.Fatalf("%s output missing %q:\n%s", tc.stage, tc.want, glsl)
		}
	}
	if _, err := CompileGLSL(parseShaderTest(t, `shader Bad { geometry(input: hexagons) {} }`), "geometry"); err == nil {
		t.Fatal("accepted an invalid geometry primitive")
	}
}

func TestStorageImageLowering(t *testing.T) {
	s := parseShaderTest(t, `shader Image { storage_image texture2d image: write; compute {} }`)
	glsl, err := CompileGLSL(s, "compute")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(glsl, "uniform writeonly image2D image;") {
		t.Fatalf("storage image was not lowered:\n%s", glsl)
	}
}

func TestStorageImageSPIRV(t *testing.T) {
	glslang, err := exec.LookPath("glslangValidator")
	if err != nil {
		t.Skip("glslangValidator unavailable")
	}
	s := parseShaderTest(t, `shader Image { storage_image texture2d image: read_write; compute { imageStore(image, ivec2(0, 0), vec4(1.0)); } }`)
	if _, err := CompileSPIRV(context.Background(), s, "compute", glslang); err != nil {
		t.Fatal(err)
	}
}
