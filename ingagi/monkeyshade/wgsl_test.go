package shader

import (
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestWGSLRoundtrip(t *testing.T) {
	naga := os.Getenv("INGAGI_NAGA")
	if naga == "" {
		var err error
		naga, err = exec.LookPath("naga")
		if err != nil {
			t.Skip("Naga unavailable")
		}
	}
	if _, err := exec.LookPath("glslangValidator"); err != nil {
		t.Skip("glslang unavailable")
	}
	validator, err := exec.LookPath("spirv-val")
	if err != nil {
		t.Skip("spirv-val unavailable")
	}
	for _, source := range []string{computeTest, `shader Vertex { vertex { gl_Position = vec4(0.0, 0.0, 0.0, 1.0); } }`, `shader Depth { fragment { gl_FragDepth = 0.5; } }`} {
		s := parseShaderTest(t, source)
		stage := s.Stages[0].Kind
		t.Run(stage, func(t *testing.T) {
			text, err := CompileArtifact(context.Background(), s, stage, "wgsl", ToolOptions{Naga: naga})
			if err != nil {
				t.Fatal(err)
			}
			if !strings.Contains(string(text), "@"+stage) {
				t.Fatalf("missing stage in WGSL: %s", text)
			}
			if stage == "compute" && (!strings.Contains(string(text), "@group(0)") || !strings.Contains(string(text), "@binding(0)")) {
				t.Fatalf("lost resource bindings: %s", text)
			}
			dir := t.TempDir()
			src, dst := filepath.Join(dir, "shader.wgsl"), filepath.Join(dir, "shader.spv")
			if err := os.WriteFile(src, text, 0600); err != nil {
				t.Fatal(err)
			}
			if out, err := exec.Command(naga, src, dst).CombinedOutput(); err != nil {
				t.Fatalf("%s\n%s: %v", text, out, err)
			}
			if out, err := exec.Command(validator, dst).CombinedOutput(); err != nil {
				t.Fatalf("%s: %v", out, err)
			}
		})
	}
}
