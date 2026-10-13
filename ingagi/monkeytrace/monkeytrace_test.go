package monkeytrace

import (
	"bytes"
	"context"
	"encoding/json"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"github.com/Chubek/JoltFX/ingagi/ecs"
	"github.com/Chubek/JoltFX/ingagi/shader"
)

func TestIntersections(t *testing.T) {
	s := primitive{A: Vec3{0, 0, -3}, Radius: 1}
	if d := intersect(s, Vec3{}, Vec3{0, 0, -1}); math.Abs(d-2) > 1e-6 {
		t.Fatalf("sphere distance %g", d)
	}
	if d := intersect(s, Vec3{0, 0, -3}, Vec3{0, 0, -1}); math.Abs(d-1) > 1e-6 {
		t.Fatalf("inside sphere %g", d)
	}
	tri := primitive{A: Vec3{-1, -1, -2}, B: Vec3{1, -1, -2}, C: Vec3{0, 1, -2}}
	if d := intersect(tri, Vec3{}, Vec3{0, 0, -1}); math.Abs(d-2) > 1e-6 {
		t.Fatalf("triangle distance %g", d)
	}
	if d := intersect(tri, Vec3{}, Vec3{0, 1, 0}); d != noHit {
		t.Fatalf("parallel triangle %g", d)
	}
}

func TestIngagiSceneAuthoring(t *testing.T) {
	scene, err := LoadScene(DemoSource)
	if err != nil {
		t.Fatal(err)
	}
	options := Options{Width: 17, Height: 9, Samples: 4, Bounces: 3}
	fromScript, err := Render(context.Background(), scene, options, CPUBackend{})
	if err != nil {
		t.Fatal(err)
	}
	fromGo, err := Render(context.Background(), DemoScene(), options, CPUBackend{})
	if err != nil {
		t.Fatal(err)
	}
	if !bytes.Equal(fromScript.Pix, fromGo.Pix) {
		t.Fatal("VM-authored ECS scene differs from reference")
	}
	// Returned handles work with Ingagi's ECS builtins.
	removed, err := LoadScene(`fn main() { let id = trace_sphere(vec3(0.0,0.0,-3.0),1.0,vec3(1.0,1.0,1.0),0.0,0.0); despawn(id); }`)
	if err != nil || removed.World.Count() != 0 {
		t.Fatal("ECS handle interoperability", err)
	}
	for _, source := range []string{
		`fn main() { trace_sphere(1.0,1.0,vec3(1.0,1.0,1.0),0.0,0.0); }`,
		`fn main() { trace_sphere(vec3(0.0,0.0,0.0),-1.0,vec3(1.0,1.0,1.0),0.0,0.0); }`,
		`fn main() { loop {} }`,
	} {
		if _, err := LoadScene(source); err == nil {
			t.Fatal("accepted invalid/budget-exhausting scene", source)
		}
	}
}

func TestECSRendering(t *testing.T) {
	scene := DemoScene()
	opts := Options{Width: 32, Height: 24, Samples: 4, Bounces: 3}
	a, err := Render(context.Background(), scene, opts, CPUBackend{})
	if err != nil {
		t.Fatal(err)
	}
	b, err := Render(context.Background(), scene, opts, CPUBackend{})
	if err != nil || !bytes.Equal(a.Pix, b.Pix) {
		t.Fatal("render is not deterministic", err)
	}
	id := scene.World.Query([]string{SphereComponent}, nil)[0]
	scene.World.Despawn(id)
	c, err := Render(context.Background(), scene, opts, CPUBackend{})
	if err != nil || bytes.Equal(a.Pix, c.Pix) {
		t.Fatal("despawn did not affect render", err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := Render(ctx, scene, opts, CPUBackend{}); err == nil {
		t.Fatal("ignored cancellation")
	}
}

func TestValidation(t *testing.T) {
	s := DemoScene()
	for _, o := range []Options{{}, {Width: -1, Height: 1, Samples: 1}, {Width: 1, Height: 1, Samples: 65}, {Width: 1, Height: 1, Samples: 1, Bounces: 9}} {
		if _, err := Render(context.Background(), s, o, CPUBackend{}); err == nil {
			t.Fatalf("accepted %+v", o)
		}
	}
	s.Camera.Target = s.Camera.Position
	if _, err := Snapshot(s, Options{Width: 1, Height: 1, Samples: 1}); err == nil {
		t.Fatal("accepted degenerate camera")
	}
	s = DemoScene()
	s.World.Spawn(map[string]map[string]any{SphereComponent: {"value": "wrong"}, MaterialComponent: {"value": Material{}}})
	if _, err := Snapshot(s, Options{Width: 1, Height: 1, Samples: 1}); err == nil {
		t.Fatal("ignored malformed component")
	}
	empty := Scene{World: ecs.NewWorld(), Camera: DefaultCamera()}
	if _, err := Snapshot(empty, Options{Width: 1, Height: 1, Samples: 1}); err != nil {
		t.Fatal("empty world", err)
	}
	for i := 0; i <= MaxPrimitives; i++ {
		empty.AddSphere(Sphere{Center: Vec3{Z: -3}, Radius: 1}, Material{Albedo: Vec3{X: 1}})
	}
	if _, err := Snapshot(empty, Options{Width: 1, Height: 1, Samples: 1}); err == nil {
		t.Fatal("accepted over-capacity scene")
	}
	bad := DemoScene()
	bad.SkyLow.X = math.NaN()
	if _, err := Snapshot(bad, Options{Width: 1, Height: 1, Samples: 1}); err == nil {
		t.Fatal("accepted nonfinite color")
	}
}

func TestShaderArtifacts(t *testing.T) {
	if _, err := ShaderArtifact(context.Background(), "glsl", shader.ToolOptions{}); err != nil {
		t.Fatal(err)
	}
	if _, err := exec.LookPath("glslangValidator"); err != nil {
		t.Skip("glslangValidator unavailable")
	}
	data, err := ShaderArtifact(context.Background(), "spirv", shader.ToolOptions{})
	if err != nil || len(data) < 20 {
		t.Fatal(err)
	}
	if validator, err := exec.LookPath("spirv-val"); err == nil {
		file := filepath.Join(t.TempDir(), "trace.spv")
		if err := os.WriteFile(file, data, 0600); err != nil {
			t.Fatal(err)
		}
		if out, err := exec.Command(validator, "--target-env", "vulkan1.1", file).CombinedOutput(); err != nil {
			t.Fatalf("%s: %v", out, err)
		}
	}
}

func TestShadingAndOwnedSnapshot(t *testing.T) {
	s := Scene{World: ecs.NewWorld(), Camera: Camera{Vec3{}, Vec3{0, 0, -1}, Vec3{0, 1, 0}, 45}, SkyLow: Vec3{1, 1, 1}, SkyHigh: Vec3{1, 1, 1}, LightDirection: Vec3{0, 0, 1}, LightIntensity: 1}
	id := s.AddSphere(Sphere{Vec3{0, 0, -3}, 1}, Material{Albedo: Vec3{1, 0, 0}})
	o := Options{Width: 1, Height: 1, Samples: 1, Bounces: 1}
	f, err := Snapshot(s, o)
	if err != nil {
		t.Fatal(err)
	}
	lit := trace(f, Vec3{}, Vec3{0, 0, -1})
	if math.Abs(lit.X-1.08) > 1e-6 || lit.Y != 0 {
		t.Fatalf("diffuse lighting: %+v", lit)
	}
	// A sphere off the primary ray occludes the directional light.
	s.LightDirection = Vec3{1, 0, 1}
	s.AddSphere(Sphere{Vec3{1, 0, -1}, 0.3}, Material{Albedo: Vec3{1, 1, 1}})
	shadow, err := Snapshot(s, o)
	if err != nil {
		t.Fatal(err)
	}
	if c := trace(shadow, Vec3{}, Vec3{0, 0, -1}); math.Abs(c.X-0.08) > 1e-6 {
		t.Fatalf("shadow: %+v", c)
	}
	s.World.Set(id, MaterialComponent, map[string]any{"value": Material{Albedo: Vec3{1, 1, 1}, Reflectivity: 1}})
	s.LightIntensity = 0
	mirror, err := Snapshot(s, o)
	if err != nil {
		t.Fatal(err)
	}
	if c := trace(mirror, Vec3{}, Vec3{0, 0, -1}); c != (Vec3{1, 1, 1}) {
		t.Fatalf("mirror sky: %+v", c)
	}
	if c := trace(f, Vec3{}, Vec3{0, 0, -1}); c != lit {
		t.Fatal("snapshot retained world data")
	}
	s.World.Set(id, MaterialComponent, map[string]any{"value": Material{Albedo: Vec3{1, 0, 0}, Emission: 2}})
	emitted, err := Snapshot(s, o)
	if err != nil {
		t.Fatal(err)
	}
	if c := trace(emitted, Vec3{}, Vec3{0, 0, -1}); math.Abs(c.X-2.08) > 1e-6 {
		t.Fatalf("emission: %+v", c)
	}
}

func TestWebGPUExport(t *testing.T) {
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
	var page bytes.Buffer
	err := WriteWebGPU(context.Background(), &page, DemoScene(), Options{Width: 17, Height: 9, Samples: 1}, shader.ToolOptions{Naga: naga})
	if err != nil {
		t.Fatal(err)
	}
	const start = `<script id="scene" type="application/json">`
	text := page.String()
	i := strings.Index(text, start) + len(start)
	j := strings.Index(text[i:], "</script>") + i
	var payload struct {
		WGSL                  string
		Geometry, Params, BVH []float32
		Width, Height         int
	}
	if err := json.Unmarshal([]byte(text[i:j]), &payload); err != nil {
		t.Fatal(err)
	}
	if payload.Width != 17 || payload.Height != 9 || len(payload.Geometry) != MaxPrimitives*16 || len(payload.BVH) != MaxBVHNodes*8 || len(payload.Params) != 36 || payload.Params[33] != 7 || !strings.Contains(payload.WGSL, "@compute") {
		t.Fatal("invalid GPU payload")
	}
	// Reparse and validate the real ray tracer, not just a toy compute shader.
	dir := t.TempDir()
	src := filepath.Join(dir, "trace.wgsl")
	dst := filepath.Join(dir, "trace.spv")
	if err := os.WriteFile(src, []byte(payload.WGSL), 0600); err != nil {
		t.Fatal(err)
	}
	if out, err := exec.Command(naga, src, dst).CombinedOutput(); err != nil {
		t.Fatalf("%s: %v", out, err)
	}
	if validator, err := exec.LookPath("spirv-val"); err == nil {
		if out, err := exec.Command(validator, dst).CombinedOutput(); err != nil {
			t.Fatalf("%s: %v", out, err)
		}
	}
}
