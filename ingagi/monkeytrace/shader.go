//go:build !monkeytrace_native

package monkeytrace

import (
	"context"
	_ "embed"
	"fmt"

	"github.com/Chubek/JoltFX/ingagi/frontend"
	"github.com/Chubek/JoltFX/ingagi/shader"
)

//go:embed trace.ing
var ShaderSource string

// ShaderArtifact compiles the actual Ingagi compute source, preserving bindings
// 0 (geometry), 1 (parameters), 2 (rgba32float output), 3 (BVH), and the 8x8 workgroup.
func ShaderArtifact(ctx context.Context, format string, tools shader.ToolOptions) ([]byte, error) {
	p := frontend.Parse(ShaderSource)
	if !p.OK() {
		return nil, fmt.Errorf("monkeytrace: shader parse: %v", p.Errors)
	}
	if len(p.File.Decls) != 1 {
		return nil, fmt.Errorf("monkeytrace: expected one shader")
	}
	s, ok := p.File.Decls[0].(*frontend.ShaderDecl)
	if !ok {
		return nil, fmt.Errorf("monkeytrace: expected shader declaration")
	}
	return shader.CompileArtifact(ctx, s, "compute", format, tools)
}

// GeometryData returns a fresh, explicitly padded std430 buffer for binding 0.
// It contains MaxPrimitives * 4 vec4s (256 KiB), including zeroed unused slots.
func (f *Frame) GeometryData() []float32 {
	data := make([]float32, MaxPrimitives*16)
	for i, p := range f.primitives {
		vals := []float64{p.A.X, p.A.Y, p.A.Z, p.Radius, p.B.X, p.B.Y, p.B.Z, p.Material.Reflectivity, p.C.X, p.C.Y, p.C.Z, 0, p.Material.Albedo.X, p.Material.Albedo.Y, p.Material.Albedo.Z, p.Material.Emission}
		for j, v := range vals {
			data[i*16+j] = float32(v)
		}
	}
	return data
}

// ParameterData returns a fresh std140-packed camera/lighting/options buffer
// containing nine vec4s (144 bytes), ready for binding 1.
func (f *Frame) ParameterData() []float32 {
	o := f.options
	vals := []float64{f.origin.X, f.origin.Y, f.origin.Z, 0, f.forward.X, f.forward.Y, f.forward.Z, f.fov, f.right.X, f.right.Y, f.right.Z, f.aspect, f.up.X, f.up.Y, f.up.Z, 0, f.skyLow.X, f.skyLow.Y, f.skyLow.Z, 0, f.skyHigh.X, f.skyHigh.Y, f.skyHigh.Z, 0, f.light.X, f.light.Y, f.light.Z, f.intensity, float64(o.Width), float64(o.Height), float64(len(f.primitives)), float64(o.Bounces), float64(o.Samples), float64(len(f.nodes)), 0, 0}
	data := make([]float32, len(vals))
	for i, v := range vals {
		data[i] = float32(v)
	}
	return data
}
