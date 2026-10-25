// Package shader preserves the original Ingagi shader import path. The actual
// GLSL/SPIR-V/cross-compilation implementation lives in monkeyshade; there is no
// duplicated lowering implementation or subprocess driver here.
package shader

import (
	"context"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	monkeyshade "github.com/Chubek/JoltFX/ingagi/monkeyshade"
)

type ToolOptions = monkeyshade.ToolOptions
type Declaration = fe.ShaderDecl
type InterfaceVariable = monkeyshade.InterfaceVariable
type FieldReflection = monkeyshade.FieldReflection
type ResourceReflection = monkeyshade.ResourceReflection
type SpecializationReflection = monkeyshade.SpecializationReflection
type StageReflection = monkeyshade.StageReflection
type PipelineReflection = monkeyshade.PipelineReflection
type CapabilityInfo = monkeyshade.CapabilityInfo
type FilamentParameter = monkeyshade.FilamentParameter
type FilamentReflection = monkeyshade.FilamentReflection

func IsFilament(s *fe.ShaderDecl) bool                 { return monkeyshade.IsFilament(s) }
func CompileFilament(s *fe.ShaderDecl) (string, error) { return monkeyshade.CompileFilament(s) }
func ReflectFilament(s *fe.ShaderDecl) (*FilamentReflection, error) {
	return monkeyshade.ReflectFilament(s)
}
func CompileFilamat(ctx context.Context, s *fe.ShaderDecl, tools ToolOptions) ([]byte, error) {
	return monkeyshade.CompileFilamat(ctx, s, tools)
}

const MaxInterfaceLocations = monkeyshade.MaxInterfaceLocations
const MaxBufferBytes = monkeyshade.MaxBufferBytes

func Reflect(s *fe.ShaderDecl, stage string) (*StageReflection, error) {
	return monkeyshade.Reflect(s, stage)
}
func LinkGraphics(vertex, fragment *fe.ShaderDecl) (*PipelineReflection, error) {
	return monkeyshade.LinkGraphics(vertex, fragment)
}
func ReflectPipeline(file *fe.File, p *fe.PipelineDecl) (*PipelineReflection, error) {
	return monkeyshade.ReflectPipeline(file, p)
}
func Capabilities() CapabilityInfo { return monkeyshade.Capabilities() }

func Parse(source, name string) (*fe.ShaderDecl, error) { return monkeyshade.Parse(source, name) }
func CompileSource(ctx context.Context, source, name, stage, format string, tools ToolOptions) ([]byte, error) {
	return monkeyshade.CompileSource(ctx, source, name, stage, format, tools)
}
func CompileGLSL(s *fe.ShaderDecl, stage string) (string, error) {
	return monkeyshade.CompileGLSL(s, stage)
}
func CompileSPIRV(ctx context.Context, s *fe.ShaderDecl, stage, executable string) ([]byte, error) {
	return monkeyshade.CompileSPIRV(ctx, s, stage, executable)
}
func CompileArtifact(ctx context.Context, s *fe.ShaderDecl, stage, format string, tools ToolOptions) ([]byte, error) {
	return monkeyshade.CompileArtifact(ctx, s, stage, format, tools)
}
