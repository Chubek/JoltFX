package monkeytrace

import (
	"context"
	"fmt"
	"image"
	"math"
)

const epsilon = 0.0001
const noHit = 1e30

// Backend renders an immutable snapshot to a display-ready, tone-mapped image.
type Backend interface {
	Render(context.Context, *Frame) (*image.RGBA, error)
}

func Render(ctx context.Context, s Scene, o Options, b Backend) (*image.RGBA, error) {
	if ctx == nil || b == nil {
		return nil, fmt.Errorf("monkeytrace: context and backend required")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	f, err := Snapshot(s, o)
	if err != nil {
		return nil, err
	}
	return b.Render(ctx, f)
}

// CPUBackend is a deterministic, sequential reference renderer. It does not
// claim to execute GPU shaders; it shares the shader's scene layout/algorithm.
type CPUBackend struct{}

func (CPUBackend) Render(ctx context.Context, f *Frame) (*image.RGBA, error) {
	if ctx == nil || f == nil {
		return nil, fmt.Errorf("monkeytrace: context and frame required")
	}
	if err := f.options.validate(); err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	o := f.options
	img := image.NewRGBA(image.Rect(0, 0, o.Width, o.Height))
	grid := math.Ceil(math.Sqrt(float64(o.Samples)))
	for y := 0; y < o.Height; y++ {
		for x := 0; x < o.Width; x++ {
			if err := ctx.Err(); err != nil {
				return nil, err
			}
			c := Vec3{}
			for i := 0; i < o.Samples; i++ {
				sx := (float64(i%int(grid)) + 0.5) / grid
				sy := (float64(i/int(grid)) + 0.5) / grid
				px := (2*(float64(x)+sx)/float64(o.Width) - 1) * f.aspect * f.fov
				py := (1 - 2*(float64(y)+sy)/float64(o.Height)) * f.fov
				d := f.forward.add(f.right.scale(px)).add(f.up.scale(py)).unit()
				c = c.add(trace(f, f.origin, d))
			}
			c = c.scale(1 / float64(o.Samples))
			idx := img.PixOffset(x, y)
			img.Pix[idx] = display(c.X)
			img.Pix[idx+1] = display(c.Y)
			img.Pix[idx+2] = display(c.Z)
			img.Pix[idx+3] = 255
		}
	}
	return img, nil
}

func display(v float64) uint8 {
	return uint8(math.Round(math.Pow(math.Max(v, 0)/(1+math.Max(v, 0)), 1/2.2) * 255))
}

func intersect(p primitive, origin, dir Vec3) float64 {
	if p.Radius > 0 {
		oc := origin.sub(p.A)
		b := oc.dot(dir)
		disc := b*b - oc.dot(oc) + p.Radius*p.Radius
		if disc < 0 {
			return noHit
		}
		r := math.Sqrt(disc)
		t := -b - r
		if t <= epsilon {
			t = -b + r
		}
		if t > epsilon {
			return t
		}
		return noHit
	}
	e1 := p.B.sub(p.A)
	e2 := p.C.sub(p.A)
	h := dir.cross(e2)
	det := e1.dot(h)
	if math.Abs(det) < 1e-8 {
		return noHit
	}
	s := origin.sub(p.A)
	u := s.dot(h) / det
	if u < 0 || u > 1 {
		return noHit
	}
	q := s.cross(e1)
	v := dir.dot(q) / det
	if v < 0 || u+v > 1 {
		return noHit
	}
	t := e2.dot(q) / det
	if t > epsilon {
		return t
	}
	return noHit
}

func trace(f *Frame, origin, dir Vec3) Vec3 {
	result := Vec3{}
	throughput := Vec3{1, 1, 1}
	for bounce := 0; bounce <= f.options.Bounces; bounce++ {
		best, hit := f.closestHit(origin, dir)
		if hit < 0 {
			t := 0.5 * (dir.Y + 1)
			sky := f.skyLow.scale(1 - t).add(f.skyHigh.scale(t))
			return result.add(throughput.mul(sky))
		}
		p := f.primitives[hit]
		pos := origin.add(dir.scale(best))
		n := p.B.sub(p.A).cross(p.C.sub(p.A))
		if p.Radius > 0 {
			n = pos.sub(p.A)
		}
		n = n.unit()
		if n.dot(dir) > 0 {
			n = n.scale(-1)
		}
		origin = offsetRayOrigin(pos, n)
		light := math.Max(0, n.dot(f.light)) * f.intensity
		if light > 0 && f.occluded(origin, f.light) {
			light = 0
		}
		m := p.Material
		shaded := m.Albedo.scale(m.Emission + (1-m.Reflectivity)*(0.08+light))
		result = result.add(throughput.mul(shaded))
		throughput = throughput.mul(m.Albedo).scale(m.Reflectivity)
		if math.Max(throughput.X, math.Max(throughput.Y, throughput.Z)) < 0.001 {
			break
		}
		dir = dir.sub(n.scale(2 * dir.dot(n))).unit()
	}
	return result
}

// A fixed bias alone vanishes below a float32 ULP in large world coordinates.
// Use the same scale-aware bias as the shader to avoid self-shadow/reflection hits.
func offsetRayOrigin(pos, normal Vec3) Vec3 {
	scale := math.Max(math.Abs(pos.X), math.Max(math.Abs(pos.Y), math.Abs(pos.Z)))
	return pos.add(normal.scale(epsilon*4 + scale*1e-6))
}
