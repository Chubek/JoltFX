package monkeytrace

import (
	"context"
	"image/png"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"

	"github.com/Chubek/JoltFX/ingagi/shader"
)

// Opt in to real browser execution; the portable suite needs no JS dependencies.
// The Node runner checks GPU pixels against CPU output and repeats readback.
func TestWebGPUTraversal(t *testing.T) {
	if os.Getenv("MONKEYTRACE_WEBGPU_TEST") != "1" {
		t.Skip("set MONKEYTRACE_WEBGPU_TEST=1 for real browser traversal tests")
	}
	node, err := exec.LookPath("node")
	if err != nil {
		t.Fatal(err)
	}
	source, err := os.ReadFile("many_spheres.ing")
	if err != nil {
		t.Fatal(err)
	}
	procedural, err := LoadScene(string(source))
	if err != nil {
		t.Fatal(err)
	}
	capacity := DefaultScene()
	for i := 0; i < MaxPrimitives; i++ {
		capacity.AddSphere(Sphere{Vec3{float64(i) * 3, 0, -3}, 1}, Material{Albedo: Vec3{0.8, 0.2, 0.1}})
	}
	// Aim at the final primitive, exercising the end of the maximum-size tree
	// and large-coordinate secondary-ray offsets in the shader.
	x := float64(MaxPrimitives-1) * 3
	capacity.Camera = Camera{Vec3{x, 0, 5}, Vec3{x, 0, -3}, Vec3{0, 1, 0}, 45}
	for _, test := range []struct {
		name  string
		scene Scene
	}{
		{"empty", DefaultScene()}, {"demo", DemoScene()}, {"procedural", procedural}, {"capacity", capacity},
	} {
		t.Run(test.name, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
			defer cancel()
			options := Options{Width: 17, Height: 9, Samples: 4, Bounces: 3}
			img, err := Render(ctx, test.scene, options, CPUBackend{})
			if err != nil {
				t.Fatal(err)
			}
			dir := t.TempDir()
			cpuFile, htmlFile := filepath.Join(dir, "cpu.png"), filepath.Join(dir, "gpu.html")
			cpu, err := os.Create(cpuFile)
			if err != nil {
				t.Fatal(err)
			}
			err = png.Encode(cpu, img)
			closeErr := cpu.Close()
			if err != nil {
				t.Fatal(err)
			}
			if closeErr != nil {
				t.Fatal(closeErr)
			}
			html, err := os.Create(htmlFile)
			if err != nil {
				t.Fatal(err)
			}
			err = WriteWebGPU(ctx, html, test.scene, options, shader.ToolOptions{})
			closeErr = html.Close()
			if err != nil {
				t.Fatal(err)
			}
			if closeErr != nil {
				t.Fatal(closeErr)
			}
			out, err := exec.CommandContext(ctx, node, "scripts/test-webgpu.mjs", htmlFile, cpuFile).CombinedOutput()
			if err != nil {
				t.Fatalf("browser traversal: %v\n%s", err, out)
			}
			t.Log(string(out))
		})
	}
}
