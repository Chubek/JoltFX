package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

const pipelineSource = `shader V {
 interface Varyings {@location(0) uv:vec2;}
 vertex(outputs:Varyings){outputs.uv=vec2(0.0);gl_Position=vec4(0.0);}
}
shader F {interface Varyings {@location(0) uv:vec2;} fragment(inputs:Varyings){} }
pipeline Draw {vertex:V::vertex;fragment:F::fragment;}`

func TestShaderReflectionCLI(t *testing.T) {
	dir := t.TempDir()
	source, output := filepath.Join(dir, "draw.ing"), filepath.Join(dir, "draw.json")
	if err := os.WriteFile(source, []byte(pipelineSource), 0600); err != nil {
		t.Fatal(err)
	}
	args := []string{"shader", "-pipeline", "Draw", "-emit", "reflect", "-glslang", "/nonexistent/glslang", "-o", output, source}
	var out, diag bytes.Buffer
	if code := runCLI(args, &out, &diag); code != 0 {
		t.Fatalf("%d: %s", code, &diag)
	}
	old, err := os.ReadFile(output)
	if err != nil {
		t.Fatal(err)
	}
	var report map[string]any
	if err = json.Unmarshal(old, &report); err != nil || len(report["stages"].([]any)) != 2 {
		t.Fatalf("%s: %v", old, err)
	}
	if code := runCLI([]string{"shader", "-shader", "V", "-stage", "vertex", "-emit", "reflect", "-o", filepath.Join(dir, "stage.json"), source}, &out, &diag); code != 0 {
		t.Fatalf("%d: %s", code, &diag)
	}
	// The fragment mismatch is rejected by check and pipeline reflection. Neither
	// a failed link nor a usage error may replace an existing report.
	invalid := strings.Replace(pipelineSource, "shader F {interface Varyings {@location(0) uv:vec2;}", "shader F {interface Varyings {@location(0) uv:vec3;}", 1)
	if err = os.WriteFile(source, []byte(invalid), 0600); err != nil {
		t.Fatal(err)
	}
	if code := runCLI(args, &out, &diag); code != 1 {
		t.Fatalf("%d: %s", code, &diag)
	}
	after, _ := os.ReadFile(output)
	if !bytes.Equal(after, old) {
		t.Fatal("failed link replaced report")
	}
	if code := runCLI([]string{"check", source}, &out, &diag); code != 1 {
		t.Fatal("check accepted bad stage link")
	}
	if code := runCLI([]string{"shader", "-pipeline", "Draw", "-emit", "glsl", "-o", output, source}, &out, &diag); code != 2 {
		t.Fatal("pipeline silently emitted one stage")
	}
	out.Reset()
	diag.Reset()
	if code := runCLI([]string{"shader-capabilities"}, &out, &diag); code != 0 {
		t.Fatal(code, diag.String())
	}
	if !bytes.Contains(out.Bytes(), []byte(`"device_dispatch": false`)) {
		t.Fatal(out.String())
	}
}
