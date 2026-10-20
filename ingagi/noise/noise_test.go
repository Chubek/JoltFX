package noise

import (
	"math"
	"testing"
)

func TestDeterministicNoise(t *testing.T) {
	a, b := New(42), New(43)
	different := false
	for i := -100; i < 100; i++ {
		x, y, z := float64(i)*.13, float64(i)*.31, float64(i)*.27
		v, err := a.Sample3(x, y, z)
		if err != nil || math.IsNaN(v) || v < -1 || v > 1 {
			t.Fatalf("%g: %v", v, err)
		}
		copy, _ := New(42).Sample3(x, y, z)
		other, _ := b.Sample3(x, y, z)
		if v != copy {
			t.Fatal("nondeterministic sample")
		}
		different = different || v != other
	}
	if !different {
		t.Fatal("seed had no effect")
	}
	for _, x := range []float64{-2, -1, 0, 1, 2} {
		v, err := a.Sample3(x, 0, 0)
		if err != nil || v != 0 {
			t.Fatalf("lattice point %g: %g %v", x, v, err)
		}
		left, _ := a.Sample3(x-1e-6, .3, .7)
		right, _ := a.Sample3(x+1e-6, .3, .7)
		if math.Abs(left-right) > 1e-4 {
			t.Fatal("lattice seam")
		}
	}
	for _, v := range []float64{math.NaN(), math.Inf(1), 1e20} {
		if _, err := a.Sample3(v, 0, 0); err == nil {
			t.Fatal("invalid coordinate accepted")
		}
	}
}

func TestProfiles(t *testing.T) {
	p := DefaultProfile(9)
	p.Octaves = 6
	n, err := NewProfile(p)
	if err != nil {
		t.Fatal(err)
	}
	v, err := n.Sample3(.17, .29, .41)
	if err != nil || math.Abs(v) > 1 {
		t.Fatalf("profile: %g %v", v, err)
	}
	for _, text := range []string{`{"seed":9,"octaves":6,"frequency":1,"lacunarity":2,"gain":0.5}`, string(mustJSON(t, p))} {
		decoded, err := ParseProfile(text)
		if err != nil {
			t.Fatal(err)
		}
		sampler, err := NewProfile(decoded)
		if err != nil {
			t.Fatal(err)
		}
		other, _ := sampler.Sample3(.17, .29, .41)
		if other != v {
			t.Fatal("profile roundtrip changed sample")
		}
	}
	for _, text := range []string{`{`, `{"unknown":1}`, `{"octaves":0}`, `{"octaves":33}`, `{"frequency":0}`, `{"gain":2}`, `{"seed":1,"seed":2}`, `{} {}`, `null`} {
		if _, err := ParseProfile(text); err == nil {
			t.Fatalf("accepted %s", text)
		}
	}
	for _, p := range []Profile{{}, {Octaves: 33}, {Octaves: 1, Frequency: math.NaN()}, {Octaves: 1, Frequency: 1, Lacunarity: math.Inf(1)}} {
		if _, err := NewProfile(p); err == nil {
			t.Fatal("invalid profile accepted")
		}
	}
}

func TestStableProfileSample(t *testing.T) {
	p := DefaultProfile(42)
	p.Octaves = 6
	n, err := NewProfile(p)
	if err != nil {
		t.Fatal(err)
	}
	value, err := n.Sample3(.17, .29, .41)
	if err != nil || math.Abs(value-.03737364310300562) > 1e-14 {
		t.Fatalf("stable seed changed: %.17g %v", value, err)
	}
}

func FuzzProfile(f *testing.F) {
	for _, source := range []string{`{}`, `{"seed":42}`, `null`, `{"gain":null}`} {
		f.Add(source)
	}
	f.Fuzz(func(t *testing.T, source string) {
		p, err := ParseProfile(source)
		if err != nil {
			return
		}
		n, err := NewProfile(p)
		if err != nil {
			t.Fatal(err)
		}
		v, err := n.Sample3(.17, .29, .41)
		if err == nil && (math.IsNaN(v) || math.IsInf(v, 0) || math.Abs(v) > 1) {
			t.Fatal("invalid sample")
		}
	})
}

func mustJSON(t *testing.T, p Profile) []byte {
	t.Helper()
	b, err := p.MarshalJSON()
	if err != nil {
		t.Fatal(err)
	}
	return b
}
