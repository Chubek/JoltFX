// Build with -buildmode=c-shared for the MonkeyTrace native CPU bridge.
package main

/*
#include "monkeytrace.h"
*/
import "C"

import (
	"context"
	"math"
	"time"
	"unsafe"

	"github.com/Chubek/JoltFX/ingagi/monkeytrace"
)

//export monkeytrace_render
func monkeytrace_render(desc *C.monkeytrace_render_desc_t, out *C.uint8_t, capacity C.size_t) (status C.int) {
	status = C.MONKEYTRACE_FAILED
	defer func() {
		if recover() != nil {
			status = C.MONKEYTRACE_FAILED
		}
	}()
	if desc == nil || out == nil || desc.size < C.size_t(C.sizeof_monkeytrace_render_desc_t) {
		return C.MONKEYTRACE_INVALID_ARGUMENT
	}
	if desc.triangle_count > monkeytrace.MaxPrimitives {
		return C.MONKEYTRACE_LIMIT
	}
	if desc.width == 0 || desc.height == 0 || desc.width > 4096 || desc.height > 4096 || uint64(desc.width)*uint64(desc.height) > 4*1024*1024 || uint64(capacity) < uint64(desc.width)*uint64(desc.height)*4 || desc.samples < 1 || desc.samples > 64 || desc.bounces > 8 || desc.timeout_ms < 1 || desc.timeout_ms > 60000 || (desc.triangle_count > 0 && desc.triangles == nil) {
		return C.MONKEYTRACE_INVALID_ARGUMENT
	}
	if math.IsNaN(float64(desc.reflectivity)) || desc.reflectivity < 0 || desc.reflectivity > 1 {
		return C.MONKEYTRACE_INVALID_ARGUMENT
	}
	ctx, cancel := context.WithTimeout(context.Background(), time.Duration(desc.timeout_ms)*time.Millisecond)
	defer cancel()
	vec := func(v []C.float) monkeytrace.Vec3 {
		return monkeytrace.Vec3{X: float64(v[0]), Y: float64(v[1]), Z: float64(v[2])}
	}
	scene := monkeytrace.DefaultScene()
	scene.Camera = monkeytrace.Camera{Position: vec(desc.eye[:]), Target: vec(desc.target[:]), Up: vec(desc.up[:]), VerticalFOV: float64(desc.vertical_fov)}
	// Use the editor's directional light, with MonkeyTrace's gradient environment.
	scene.LightDirection = monkeytrace.Vec3{X: .4, Y: .8, Z: .3}
	scene.LightIntensity = 1.8
	triangles := unsafe.Slice(desc.triangles, int(desc.triangle_count))
	for _, triangle := range triangles {
		scene.AddTriangle(monkeytrace.Triangle{A: vec(triangle.xyz[:3]), B: vec(triangle.xyz[3:6]), C: vec(triangle.xyz[6:9])}, monkeytrace.Material{Albedo: vec(triangle.albedo[:]), Reflectivity: float64(desc.reflectivity)})
	}
	image, err := monkeytrace.Render(ctx, scene, monkeytrace.Options{Width: int(desc.width), Height: int(desc.height), Samples: int(desc.samples), Bounces: int(desc.bounces)}, monkeytrace.CPUBackend{})
	if err != nil {
		if ctx.Err() != nil {
			return C.MONKEYTRACE_TIMEOUT
		}
		return C.MONKEYTRACE_INVALID_ARGUMENT
	}
	copy(unsafe.Slice((*byte)(unsafe.Pointer(out)), len(image.Pix)), image.Pix)
	return C.MONKEYTRACE_SUCCESS
}

func main() {}
