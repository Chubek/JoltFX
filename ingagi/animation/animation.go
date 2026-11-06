// Package animation implements bounded, renderer-neutral 2D animation geometry
// and timing. Coordinates are stage pixels; angles are radians, time is seconds.
package animation

import (
	"fmt"
	"math"
)

const MaxPoints = 2048
const MaxShapes = 256
const MaxFrames = 240
const MaxTotalPoints = 1000000

type Point struct{ X, Y float64 }

func finite(v float64) bool  { return !math.IsNaN(v) && !math.IsInf(v, 0) }
func unit(t float64) float64 { return math.Max(0, math.Min(1, t)) }
func ValidatePoints(p []Point) error {
	if len(p) < 2 || len(p) > MaxPoints {
		return fmt.Errorf("animation: expected 2..%d points", MaxPoints)
	}
	for _, v := range p {
		if !finite(v.X) || !finite(v.Y) || math.Abs(v.X) > 100000 || math.Abs(v.Y) > 100000 {
			return fmt.Errorf("animation: invalid point")
		}
	}
	return nil
}
func Bezier(p []Point, t float64) (Point, error) {
	if err := ValidatePoints(p); err != nil {
		return Point{}, err
	}
	if len(p) > 4 || !finite(t) {
		return Point{}, fmt.Errorf("animation: Bezier needs 2..4 controls and finite time")
	}
	return bezier(p, unit(t)), nil
}
func Curve(p []Point, steps int) ([]Point, error) {
	if steps < 1 || steps >= MaxPoints {
		return nil, fmt.Errorf("animation: curve subdivisions out of range")
	}
	if _, err := Bezier(p, 0); err != nil {
		return nil, err
	}
	out := make([]Point, steps+1)
	for i := range out {
		out[i] = bezier(p, float64(i)/float64(steps))
	}
	return out, nil
}
func Length(p []Point) (float64, error) {
	if err := ValidatePoints(p); err != nil {
		return 0, err
	}
	n := 0.0
	for i := 1; i < len(p); i++ {
		n += math.Hypot(p[i].X-p[i-1].X, p[i].Y-p[i-1].Y)
	}
	return n, nil
}
func Along(p []Point, t float64) (Point, error) {
	length, err := Length(p)
	if err != nil {
		return Point{}, err
	}
	if !finite(t) {
		return Point{}, fmt.Errorf("animation: nonfinite path time")
	}
	d := unit(t) * length
	for i := 1; i < len(p); i++ {
		n := math.Hypot(p[i].X-p[i-1].X, p[i].Y-p[i-1].Y)
		if n > 0 && d <= n {
			u := d / n
			return Point{p[i-1].X*(1-u) + p[i].X*u, p[i-1].Y*(1-u) + p[i].Y*u}, nil
		}
		d -= n
	}
	return p[len(p)-1], nil
}
func Transform(p []Point, m [6]float64) ([]Point, error) {
	if err := ValidatePoints(p); err != nil {
		return nil, err
	}
	for _, v := range m {
		if !finite(v) {
			return nil, fmt.Errorf("animation: nonfinite transform")
		}
	}
	out := make([]Point, len(p))
	for i, v := range p {
		out[i] = Point{m[0]*v.X + m[2]*v.Y + m[4], m[1]*v.X + m[3]*v.Y + m[5]}
	}
	return out, ValidatePoints(out)
}
func Morph(a, b []Point, t float64) ([]Point, error) {
	if err := ValidatePoints(a); err != nil {
		return nil, err
	}
	if err := ValidatePoints(b); err != nil {
		return nil, err
	}
	if len(a) != len(b) || !finite(t) {
		return nil, fmt.Errorf("animation: morph needs equal point counts and finite time")
	}
	t = unit(t)
	out := make([]Point, len(a))
	for i := range a {
		out[i] = Point{a[i].X*(1-t) + b[i].X*t, a[i].Y*(1-t) + b[i].Y*t}
	}
	return out, nil
}
func Ease(name string, t float64) (float64, error) {
	if !finite(t) {
		return 0, fmt.Errorf("animation: nonfinite easing time")
	}
	t = unit(t)
	switch name {
	case "step":
		if t < 1 {
			return 0, nil
		}
		return 1, nil
	case "linear":
		return t, nil
	case "smooth":
		return t * t * (3 - 2*t), nil
	case "smoother":
		return t * t * t * (t*(6*t-15) + 10), nil
	case "quad-in":
		return t * t, nil
	case "quad-out":
		return t * (2 - t), nil
	case "quad-in-out":
		if t < .5 {
			return 2 * t * t, nil
		}
		return 1 - math.Pow(-2*t+2, 2)/2, nil
	case "cubic-in":
		return t * t * t, nil
	case "cubic-out":
		return 1 - math.Pow(1-t, 3), nil
	case "cubic-in-out":
		if t < .5 {
			return 4 * t * t * t, nil
		}
		return 1 - math.Pow(-2*t+2, 3)/2, nil
	case "sine-in-out":
		return (1 - math.Cos(math.Pi*t)) / 2, nil
	case "expo-in-out":
		if t == 0 || t == 1 {
			return t, nil
		}
		if t < .5 {
			return math.Pow(2, 20*t-10) / 2, nil
		}
		return (2 - math.Pow(2, -20*t+10)) / 2, nil
	case "back-out":
		u := t - 1
		return 1 + 2.70158*u*u*u + 1.70158*u*u, nil
	case "bounce-out":
		const n = 7.5625
		const d = 2.75
		if t < 1/d {
			return n * t * t, nil
		}
		if t < 2/d {
			t -= 1.5 / d
			return n*t*t + .75, nil
		}
		if t < 2.5/d {
			t -= 2.25 / d
			return n*t*t + .9375, nil
		}
		t -= 2.625 / d
		return n*t*t + .984375, nil
	case "elastic-out":
		if t == 0 || t == 1 {
			return t, nil
		}
		return math.Pow(2, -10*t)*math.Sin((t*10-.75)*2*math.Pi/3) + 1, nil
	}
	return 0, fmt.Errorf("animation: unknown easing %q", name)
}
func Track(times, values []float64, t float64, ease string) (float64, error) {
	if len(times) == 0 || len(times) > MaxPoints || len(times) != len(values) || !finite(t) {
		return 0, fmt.Errorf("animation: invalid keyframe arrays/time")
	}
	if _, err := Ease(ease, 0); err != nil {
		return 0, err
	}
	for i, v := range times {
		if !finite(v) || !finite(values[i]) || (i > 0 && (v <= times[i-1] || !finite(v-times[i-1]))) {
			return 0, fmt.Errorf("animation: keys must be finite and strictly increasing")
		}
	}
	if t <= times[0] {
		return values[0], nil
	}
	for i := 1; i < len(times); i++ {
		if t < times[i] {
			u, _ := Ease(ease, (t-times[i-1])/(times[i]-times[i-1]))
			v := values[i-1]*(1-u) + values[i]*u
			if !finite(v) {
				return 0, fmt.Errorf("animation: keyframe overflow")
			}
			return v, nil
		}
	}
	return values[len(values)-1], nil
}
