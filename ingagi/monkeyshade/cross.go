package shader

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// ToolOptions selects subprocess executables. Empty paths use the corresponding
// INGAGI_* environment variable, then PATH. All subprocesses use the caller's
// context; no tool command is interpreted by a shell.
type ToolOptions struct {
	GLSLang    string
	SPIRVCross string
	Naga       string
}

// CompileArtifact generates shader source or SPIR-V. Cross-compilation uses the
// validated SPIR-V intermediate, preserving its stage semantics and resource
// bindings. Renderer creation/dispatch belongs to the embedding host.
// Formats: glsl (Vulkan), spirv, hlsl (SM 5.0), glsl-opengl (450), glsl-es (300,
// vertex/fragment only), msl (2.0), wgsl (vertex/fragment/compute).
func CompileArtifact(ctx context.Context, s *fe.ShaderDecl, stage, format string, tools ToolOptions) ([]byte, error) {
	if ctx == nil {
		return nil, fmt.Errorf("ingagi: missing shader build context")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if format == "glsl" {
		text, err := CompileGLSL(s, stage)
		return []byte(text), err
	}
	if format == "reflect" {
		r, err := Reflect(s, stage)
		if err != nil {
			return nil, err
		}
		return json.MarshalIndent(r, "", "  ")
	}
	var flags []string
	switch format {
	case "spirv":
	case "wgsl":
		if stage != "vertex" && stage != "fragment" && stage != "compute" {
			return nil, fmt.Errorf("ingagi: WGSL does not support %s stages", stage)
		}
	case "hlsl":
		flags = []string{"--hlsl", "--shader-model", "50"}
	case "glsl-opengl":
		flags = []string{"--version", "450", "--no-es"}
	case "glsl-es":
		if stage != "vertex" && stage != "fragment" {
			return nil, fmt.Errorf("ingagi: GLSL ES 300 does not support %s stages", stage)
		}
		flags = []string{"--version", "300", "--es"}
	case "msl":
		flags = []string{"--msl", "--msl-version", "20000"}
	default:
		return nil, fmt.Errorf("ingagi: unsupported shader artifact %q", format)
	}
	glslang := tools.GLSLang
	if glslang == "" {
		glslang = os.Getenv("INGAGI_GLSLANG")
	}
	data, err := CompileSPIRV(ctx, s, stage, glslang)
	if err != nil || format == "spirv" {
		return data, err
	}
	dir, err := os.MkdirTemp("", "ingagi-cross-*")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	file := filepath.Join(dir, "shader.spv")
	if err := os.WriteFile(file, data, 0600); err != nil {
		return nil, err
	}
	if format == "wgsl" {
		naga := tools.Naga
		if naga == "" {
			naga = os.Getenv("INGAGI_NAGA")
		}
		if naga == "" {
			naga = "naga"
		}
		output := filepath.Join(dir, "shader.wgsl")
		if diagnostic, err := exec.CommandContext(ctx, naga, "--keep-coordinate-space", file, output).CombinedOutput(); err != nil {
			return nil, fmt.Errorf("ingagi: Naga: %w\n%s", err, diagnostic)
		}
		text, err := os.ReadFile(output)
		if err != nil {
			return nil, err
		}
		if len(strings.TrimSpace(string(text))) == 0 {
			return nil, fmt.Errorf("ingagi: Naga produced empty WGSL")
		}
		return text, nil
	}
	cross := tools.SPIRVCross
	if cross == "" {
		cross = os.Getenv("INGAGI_SPIRV_CROSS")
	}
	if cross == "" {
		cross = "spirv-cross"
	}
	cmd := exec.CommandContext(ctx, cross, append([]string{file}, flags...)...)
	var diagnostic strings.Builder
	cmd.Stderr = &diagnostic
	text, err := cmd.Output()
	if err != nil {
		return nil, fmt.Errorf("ingagi: SPIRV-Cross: %w\n%s", err, diagnostic.String())
	}
	if len(strings.TrimSpace(string(text))) == 0 {
		return nil, fmt.Errorf("ingagi: SPIRV-Cross produced empty %s", format)
	}
	return text, nil
}
