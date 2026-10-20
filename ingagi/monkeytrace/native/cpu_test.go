package main

import (
	"bytes"
	"context"
	"testing"

	"github.com/Chubek/JoltFX/ingagi/monkeytrace"
)

// The native CPU dependency graph must not need the compiler or shader tools.
func TestNativeCPUScene(t *testing.T) {
	s := monkeytrace.DefaultScene()
	o := monkeytrace.Options{Width: 17, Height: 9, Samples: 4, Bounces: 2}
	a, err := monkeytrace.Render(context.Background(), s, o, monkeytrace.CPUBackend{})
	if err != nil {
		t.Fatal(err)
	}
	s.AddTriangle(monkeytrace.Triangle{A: monkeytrace.Vec3{X: -2, Y: -2}, B: monkeytrace.Vec3{X: 2, Y: -2}, C: monkeytrace.Vec3{Y: 2}}, monkeytrace.Material{Albedo: monkeytrace.Vec3{X: 1}})
	b, err := monkeytrace.Render(context.Background(), s, o, monkeytrace.CPUBackend{})
	if err != nil || bytes.Equal(a.Pix, b.Pix) {
		t.Fatalf("geometry did not affect CPU render: %v", err)
	}
	c, err := monkeytrace.Render(context.Background(), s, o, monkeytrace.CPUBackend{})
	if err != nil || !bytes.Equal(b.Pix, c.Pix) {
		t.Fatalf("CPU render is not deterministic: %v", err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := monkeytrace.Render(ctx, s, o, monkeytrace.CPUBackend{}); err == nil {
		t.Fatal("cancelled render succeeded")
	}
}
