package monkeytrace

import (
	"context"
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"strings"

	"github.com/Chubek/JoltFX/ingagi/shader"
)

//go:embed webgpu.html
var webGPUPage string

// WriteWebGPU builds a self-contained browser renderer from this scene's owned
// snapshot and the Ingagi shader compiled through SPIR-V to WGSL. The browser
// requests an adapter, dispatches the shader, reads back rgba32float pixels and
// presents them on a canvas. Build tools are not needed by the browser.
func WriteWebGPU(ctx context.Context, w io.Writer, s Scene, o Options, tools shader.ToolOptions) error {
	if w == nil {
		return fmt.Errorf("monkeytrace: output writer required")
	}
	frame, err := Snapshot(s, o)
	if err != nil {
		return err
	}
	wgsl, err := ShaderArtifact(ctx, "wgsl", tools)
	if err != nil {
		return err
	}
	payload := struct {
		WGSL     string    `json:"wgsl"`
		Geometry []float32 `json:"geometry"`
		Params   []float32 `json:"params"`
		BVH      []float32 `json:"bvh"`
		Width    int       `json:"width"`
		Height   int       `json:"height"`
	}{string(wgsl), frame.GeometryData(), frame.ParameterData(), frame.BVHData(), o.Width, o.Height}
	// JSON's HTML escaping prevents shader source from terminating the script.
	data, err := json.Marshal(payload)
	if err != nil {
		return err
	}
	_, err = io.WriteString(w, strings.Replace(webGPUPage, "__MONKEYTRACE_DATA__", string(data), 1))
	return err
}
