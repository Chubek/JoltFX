package animation

import (
	"math"
	"testing"
)

func TestGeometryAndTiming(t *testing.T) {
	p := []Point{{0, 0}, {0, 10}, {10, 10}, {10, 0}}
	q, err := Bezier(p, .5)
	if err != nil || math.Abs(q.X-5) > 1e-12 || math.Abs(q.Y-7.5) > 1e-12 {
		t.Fatal(q, err)
	}
	for _, name := range []string{"linear", "smooth", "smoother", "quad-in", "quad-out", "quad-in-out", "cubic-in", "cubic-out", "cubic-in-out", "sine-in-out", "expo-in-out", "back-out", "bounce-out", "elastic-out"} {
		a, e := Ease(name, 0)
		b, f := Ease(name, 1)
		if e != nil || f != nil || math.Abs(a) > 1e-12 || math.Abs(b-1) > 1e-12 {
			t.Fatal(name, a, b, e, f)
		}
	}
	if _, err = Ease("unknown", .5); err == nil {
		t.Fatal("unknown ease")
	}
	if _, err = Bezier(p, math.NaN()); err == nil {
		t.Fatal("NaN")
	}
	v, err := Track([]float64{0, 1, 2}, []float64{0, 10, 0}, .5, "linear")
	if err != nil || v != 5 {
		t.Fatal(v, err)
	}
	if _, err = Track([]float64{0, 0}, []float64{1, 2}, 0, "linear"); err == nil {
		t.Fatal("duplicate keys")
	}
	if _, err = Track([]float64{-math.MaxFloat64, math.MaxFloat64}, []float64{1, 2}, 0, "linear"); err == nil {
		t.Fatal("overflowing key span")
	}
	path := []Point{{0, 0}, {3, 0}, {3, 4}}
	q, err = Along(path, .5)
	if err != nil || q != (Point{3, .5}) {
		t.Fatal(q, err)
	}
	transformed, err := Transform(path, [6]float64{0, 1, -1, 0, 10, 20})
	if err != nil || transformed[2] != (Point{6, 23}) || path[2] != (Point{3, 4}) {
		t.Fatal(transformed, err)
	}
}
