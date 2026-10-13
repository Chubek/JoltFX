// Package monkeytrace is an ECS-backed ray tracer with a portable CPU reference
// backend and an Ingagi compute shader dispatched by the browser WebGPU host.
package monkeytrace

import (
	"fmt"
	"math"
	"sort"

	"github.com/Chubek/JoltFX/ingagi/ecs"
)

const (
	SphereComponent   = "monkeytrace.Sphere"
	TriangleComponent = "monkeytrace.Triangle"
	MaterialComponent = "monkeytrace.Material"
	MaxPrimitives     = 4096
	MaxBVHNodes       = 2*MaxPrimitives - 1
)

// Vec3 is a world-space vector or linear-RGB color. GPU buffers use float32.
type Vec3 struct{ X, Y, Z float64 }

func (a Vec3) add(b Vec3) Vec3      { return Vec3{a.X + b.X, a.Y + b.Y, a.Z + b.Z} }
func (a Vec3) sub(b Vec3) Vec3      { return Vec3{a.X - b.X, a.Y - b.Y, a.Z - b.Z} }
func (a Vec3) scale(s float64) Vec3 { return Vec3{a.X * s, a.Y * s, a.Z * s} }
func (a Vec3) mul(b Vec3) Vec3      { return Vec3{a.X * b.X, a.Y * b.Y, a.Z * b.Z} }
func (a Vec3) dot(b Vec3) float64   { return a.X*b.X + a.Y*b.Y + a.Z*b.Z }
func (a Vec3) cross(b Vec3) Vec3 {
	return Vec3{a.Y*b.Z - a.Z*b.Y, a.Z*b.X - a.X*b.Z, a.X*b.Y - a.Y*b.X}
}
func (a Vec3) unit() Vec3 { return a.scale(1 / math.Sqrt(a.dot(a))) }
func (a Vec3) valid() bool {
	for _, v := range []float64{a.X, a.Y, a.Z} {
		if math.IsNaN(v) || math.IsInf(v, 0) || math.Abs(v) > 1e6 {
			return false
		}
	}
	return true
}

// Material uses linear RGB, a [0,1] mirror fraction and nonnegative emission.
type Material struct {
	Albedo                 Vec3
	Reflectivity, Emission float64
}

// Sphere is world-space geometry with a positive radius.
type Sphere struct {
	Center Vec3
	Radius float64
}

// Triangle is a nondegenerate, two-sided world-space triangle.
type Triangle struct{ A, B, C Vec3 }

// Camera defines a look-at perspective view, with vertical FOV in degrees.
type Camera struct {
	Position, Target, Up Vec3
	VerticalFOV          float64
}

func DefaultCamera() Camera { return Camera{Vec3{0, 1, 5}, Vec3{0, 0, -1}, Vec3{0, 1, 0}, 45} }

// Scene borrows a World. Mutations and snapshot creation must be externally
// serialized; rendering only reads an owned, immutable snapshot.
type Scene struct {
	World           *ecs.World
	Camera          Camera
	SkyLow, SkyHigh Vec3
	LightDirection  Vec3 // direction from the surface toward the directional light
	LightIntensity  float64
}

func (s Scene) AddSphere(v Sphere, m Material) ecs.EntityID {
	return s.World.Spawn(map[string]map[string]any{SphereComponent: {"value": v}, MaterialComponent: {"value": m}})
}
func (s Scene) AddTriangle(v Triangle, m Material) ecs.EntityID {
	return s.World.Spawn(map[string]map[string]any{TriangleComponent: {"value": v}, MaterialComponent: {"value": m}})
}

// Options bounds work and allocations on both backends. Bounces counts reflected
// continuations (zero still shades the primary hit); samples are deterministic.
type Options struct{ Width, Height, Samples, Bounces int }

func (o Options) validate() error {
	if o.Width < 1 || o.Height < 1 || o.Width > 4096 || o.Height > 4096 || o.Width*o.Height > 4*1024*1024 {
		return fmt.Errorf("monkeytrace: image must fit 4096 per axis and 4M pixels")
	}
	if o.Samples < 1 || o.Samples > 64 || o.Bounces < 0 || o.Bounces > 8 {
		return fmt.Errorf("monkeytrace: samples must be 1..64 and bounces 0..8")
	}
	return nil
}

type primitive struct {
	A, B, C  Vec3
	Radius   float64
	Material Material
}

// Frame is an owned scene snapshot. Its internals enforce the shared GPU layout.
type Frame struct {
	options                    Options
	primitives                 []primitive
	nodes                      []bvhNode
	origin, forward, right, up Vec3
	skyLow, skyHigh, light     Vec3
	fov, aspect, intensity     float64
}

// Options returns the validated render settings for this snapshot.
func (f *Frame) Options() Options { return f.options }

// Snapshot validates scene data and copies it out of the borrowed ECS world.
func Snapshot(s Scene, o Options) (*Frame, error) {
	if err := o.validate(); err != nil {
		return nil, err
	}
	if s.World == nil {
		return nil, fmt.Errorf("monkeytrace: missing ECS world")
	}
	c := s.Camera
	if !c.Position.valid() || !c.Target.valid() || !c.Up.valid() || !(c.VerticalFOV > 0 && c.VerticalFOV < 179) {
		return nil, fmt.Errorf("monkeytrace: invalid camera")
	}
	forward := c.Target.sub(c.Position)
	right := forward.cross(c.Up)
	if forward.dot(forward) < 1e-12 || right.dot(right) < 1e-12 {
		return nil, fmt.Errorf("monkeytrace: degenerate camera basis")
	}
	colorOK := func(v Vec3) bool { return v.valid() && v.X >= 0 && v.Y >= 0 && v.Z >= 0 }
	if !colorOK(s.SkyLow) || !colorOK(s.SkyHigh) || !s.LightDirection.valid() || !(s.LightIntensity >= 0 && s.LightIntensity <= 1e6) {
		return nil, fmt.Errorf("monkeytrace: invalid lighting")
	}
	light := s.LightDirection
	if s.LightIntensity > 0 && light.dot(light) < 1e-12 {
		return nil, fmt.Errorf("monkeytrace: missing light direction")
	}
	if light.dot(light) > 0 {
		light = light.unit()
	}
	f := &Frame{options: o, origin: c.Position, forward: forward.unit(), right: right.unit(), skyLow: s.SkyLow, skyHigh: s.SkyHigh, light: light, intensity: s.LightIntensity, fov: math.Tan(c.VerticalFOV * math.Pi / 360), aspect: float64(o.Width) / float64(o.Height)}
	f.up = f.right.cross(f.forward).unit()
	// Query every live entity once: malformed or material-less geometry is an
	// error rather than disappearing. Sorting fixes map iteration nondeterminism.
	ids := s.World.Query(nil, nil)
	sort.Slice(ids, func(i, j int) bool { return ids[i].Index < ids[j].Index })
	for _, id := range ids {
		sf, hasS := s.World.Get(id, SphereComponent)
		tf, hasT := s.World.Get(id, TriangleComponent)
		if !hasS && !hasT {
			continue
		}
		if hasS && hasT {
			return nil, fmt.Errorf("monkeytrace: entity %v has ambiguous geometry", id)
		}
		mf, _ := s.World.Get(id, MaterialComponent)
		m, ok := mf["value"].(Material)
		if !ok || !colorOK(m.Albedo) || m.Albedo.X > 1 || m.Albedo.Y > 1 || m.Albedo.Z > 1 || !(m.Reflectivity >= 0 && m.Reflectivity <= 1) || !(m.Emission >= 0 && m.Emission <= 1e6) {
			return nil, fmt.Errorf("monkeytrace: entity %v has invalid material", id)
		}
		p := primitive{Material: m}
		if hasS {
			v, ok := sf["value"].(Sphere)
			if !ok || !v.Center.valid() || !(v.Radius >= 1e-4 && v.Radius <= 1e6) {
				return nil, fmt.Errorf("monkeytrace: entity %v has invalid sphere", id)
			}
			p.A, p.Radius = v.Center, v.Radius
		} else {
			v, ok := tf["value"].(Triangle)
			n := v.B.sub(v.A).cross(v.C.sub(v.A))
			if !ok || !v.A.valid() || !v.B.valid() || !v.C.valid() || n.dot(n) < 1e-12 {
				return nil, fmt.Errorf("monkeytrace: entity %v has invalid triangle", id)
			}
			p.A, p.B, p.C = v.A, v.B, v.C
		}
		f.primitives = append(f.primitives, p)
		if len(f.primitives) > MaxPrimitives {
			return nil, fmt.Errorf("monkeytrace: maximum %d primitives exceeded", MaxPrimitives)
		}
	}
	f.buildBVH()
	return f, nil
}

// DefaultScene returns an empty ECS world with useful camera/lighting defaults.
func DefaultScene() Scene {
	return Scene{World: ecs.NewWorld(), Camera: DefaultCamera(), SkyLow: Vec3{0.65, 0.75, 0.9}, SkyHigh: Vec3{0.1, 0.25, 0.55}, LightDirection: Vec3{-1, 3, 2}, LightIntensity: 1.8}
}

func DemoScene() Scene {
	s := DefaultScene()
	s.AddSphere(Sphere{Vec3{-1, 0, -1}, 1}, Material{Vec3{0.8, 0.12, 0.08}, 0.15, 0})
	s.AddSphere(Sphere{Vec3{1.2, 0, -1.5}, 1}, Material{Vec3{0.85, 0.9, 0.95}, 0.85, 0})
	ground := Material{Vec3{0.5, 0.55, 0.45}, 0.05, 0}
	s.AddTriangle(Triangle{Vec3{-20, -1, -20}, Vec3{20, -1, -20}, Vec3{20, -1, 20}}, ground)
	s.AddTriangle(Triangle{Vec3{-20, -1, -20}, Vec3{20, -1, 20}, Vec3{-20, -1, 20}}, ground)
	return s
}
