package shader

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestCrossCompileRoundtrip(t *testing.T) {
	cross := os.Getenv("INGAGI_SPIRV_CROSS")
	if cross == "" {
		var err error
		cross, err = exec.LookPath("spirv-cross")
		if err != nil {
			t.Skip("SPIRV-Cross unavailable")
		}
	}
	glslang, err := exec.LookPath("glslangValidator")
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
		for _, format := range []string{"hlsl", "glsl-opengl", "glsl-es", "msl"} {
			if format == "glsl-es" && stage == "compute" {
				continue
			}
			t.Run(stage+"/"+format, func(t *testing.T) {
				text, err := CompileArtifact(context.Background(), s, stage, format, ToolOptions{GLSLang: glslang, SPIRVCross: cross})
				if err != nil {
					t.Fatal(err)
				}
				if format == "msl" {
					if !strings.Contains(string(text), "metal") {
						t.Fatal(string(text))
					}
					return
				}
				dir := t.TempDir()
				src, dst := filepath.Join(dir, "shader.txt"), filepath.Join(dir, "shader.spv")
				if err := os.WriteFile(src, text, 0600); err != nil {
					t.Fatal(err)
				}
				args := []string{"-G", "--auto-map-locations", "--auto-map-bindings", "-S", stageNames[stage], "-o", dst, src}
				if format == "hlsl" {
					args = []string{"-D", "-V", "-e", "main", "-S", stageNames[stage], "-o", dst, src}
				}
				// ES 300 source is validated as WebGL2 GLSL; SPIR-V needs ES 310.
				if format == "glsl-es" {
					args = []string{"-S", stageNames[stage], src}
				}
				if out, err := exec.Command(glslang, args...).CombinedOutput(); err != nil {
					t.Fatalf("%s\n%s: %v", text, out, err)
				}
				if format != "glsl-es" {
					if out, err := exec.Command(validator, dst).CombinedOutput(); err != nil {
						t.Fatalf("%s: %v", out, err)
					}
				}
			})
		}
	}
}

func TestShaderArtifactGates(t *testing.T) {
	s := parseShaderTest(t, computeTest)
	for _, format := range []string{"invalid", "glsl-es"} {
		if _, err := CompileArtifact(context.Background(), s, "compute", format, ToolOptions{}); err == nil {
			t.Fatalf("accepted %s", format)
		}
	}
	if _, err := CompileArtifact(nil, s, "compute", "glsl", ToolOptions{}); err == nil {
		t.Fatal("accepted nil context")
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := CompileArtifact(ctx, s, "compute", "glsl", ToolOptions{}); err == nil {
		t.Fatal("ignored cancellation")
	}
	if _, err := CompileSPIRV(nil, s, "compute", ""); err == nil {
		t.Fatal("accepted nil SPIR-V context")
	}
}
