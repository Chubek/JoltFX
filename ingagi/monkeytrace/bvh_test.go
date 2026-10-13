package monkeytrace

import (
	"math"
	"math/rand"
	"os"
	"testing"
)

func bruteHit(f *Frame, eye, dir Vec3) (float64, int) {
	best, hit := noHit, -1
	for i, p := range f.primitives {
		if distance := intersect(p, eye, dir); distance < best {
			best, hit = distance, i
		}
	}
	return best, hit
}

func TestBVHDifferential(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	s := DefaultScene()
	material := Material{Albedo: Vec3{0.7, 0.5, 0.3}}
	for i := 0; i < 400; i++ {
		center := Vec3{rng.Float64()*20 - 10, rng.Float64()*20 - 10, rng.Float64()*20 - 10}
		if i%2 == 0 {
			s.AddSphere(Sphere{center, 0.1 + rng.Float64()}, material)
		} else {
			s.AddTriangle(Triangle{center, center.add(Vec3{1, 0, 0}), center.add(Vec3{0, 1, 0.5})}, material)
		}
	}
	f, err := Snapshot(s, Options{Width: 1, Height: 1, Samples: 1})
	if err != nil {
		t.Fatal(err)
	}
	for i := 0; i < 2000; i++ {
		eye := Vec3{rng.Float64()*30 - 15, rng.Float64()*30 - 15, rng.Float64()*30 - 15}
		dir := Vec3{rng.Float64()*2 - 1, rng.Float64()*2 - 1, rng.Float64()*2 - 1}.unit()
		if i%4 == 0 {
			dir = Vec3{0, 0, -1}
		} // parallel slabs, including planar triangles
		wantDistance, wantID := bruteHit(f, eye, dir)
		distance, id := f.closestHit(eye, dir)
		if id != wantID || distance != wantDistance {
			t.Fatalf("ray %d: BVH (%g,%d), brute (%g,%d)", i, distance, id, wantDistance, wantID)
		}
		if f.occluded(eye, dir) != (wantID >= 0) {
			t.Fatalf("shadow ray %d", i)
		}
	}
}

func TestBVHEdgeCases(t *testing.T) {
	options := Options{Width: 1, Height: 1, Samples: 1}
	empty, err := Snapshot(DefaultScene(), options)
	if err != nil {
		t.Fatal(err)
	}
	if _, hit := empty.closestHit(Vec3{}, Vec3{0, 0, -1}); hit != -1 || empty.occluded(Vec3{}, Vec3{0, 0, -1}) {
		t.Fatal("empty BVH hit")
	}
	s := DefaultScene()
	m := Material{Albedo: Vec3{1, 1, 1}}
	for i := 0; i < 64; i++ {
		s.AddSphere(Sphere{Vec3{0, 0, -3}, 1}, m)
	}
	f, err := Snapshot(s, options)
	if err != nil {
		t.Fatal(err)
	}
	if distance, hit := f.closestHit(Vec3{}, Vec3{0, 0, -1}); distance != 2 || hit != 0 {
		t.Fatalf("coincident geometry tie: %g,%d", distance, hit)
	}
	if distance, hit := f.closestHit(Vec3{0, 0, -3}, Vec3{0, 0, 1}); distance != 1 || hit != 0 {
		t.Fatalf("inside root: %g,%d", distance, hit)
	}
	before := f.BVHData()
	s.World.Despawn(s.World.Query([]string{SphereComponent}, nil)[0])
	after := f.BVHData()
	for i := range before {
		if before[i] != after[i] {
			t.Fatal("world mutation changed snapshot BVH")
		}
	}
	// A single thin, two-sided triangle and a ray parallel to the z slab.
	s = DefaultScene()
	s.AddTriangle(Triangle{Vec3{-1, -1, -2}, Vec3{1, -1, -2}, Vec3{0, 1, -2}}, m)
	f, err = Snapshot(s, options)
	if err != nil {
		t.Fatal(err)
	}
	if distance, hit := f.closestHit(Vec3{}, Vec3{0, 0, -1}); hit != 0 || distance != 2 {
		t.Fatal("planar triangle was culled")
	}
	if f.occluded(Vec3{}, Vec3{1, 0, 0}) {
		t.Fatal("parallel outside slab hit")
	}
}

func TestBVHCapacityAndPruning(t *testing.T) {
	s := DefaultScene()
	m := Material{Albedo: Vec3{1, 1, 1}}
	for i := 0; i < MaxPrimitives; i++ {
		s.AddSphere(Sphere{Vec3{float64(i) * 3, 0, -3}, 1}, m)
	}
	f, err := Snapshot(s, Options{Width: 1, Height: 1, Samples: 1})
	if err != nil {
		t.Fatal(err)
	}
	if len(f.nodes) != MaxBVHNodes {
		t.Fatalf("node count %d", len(f.nodes))
	}
	distance, hit, tests := f.traverse(Vec3{}, Vec3{0, 0, -1}, false)
	if distance != 2 || hit != 0 {
		t.Fatal("large scene hit", distance, hit)
	}
	if tests > 16 {
		t.Fatalf("BVH failed to prune: %d primitive tests", tests)
	}
	for i, node := range f.nodes {
		if node.skip <= i || node.skip > len(f.nodes) {
			t.Fatalf("invalid skip link at %d", i)
		}
		for _, v := range []float64{node.min.X, node.min.Y, node.min.Z, node.max.X, node.max.Y, node.max.Z} {
			if math.IsNaN(v) || math.IsInf(v, 0) {
				t.Fatal("nonfinite bounds")
			}
		}
	}
}

func TestLargeCoordinateOriginBias(t *testing.T) {
	p := primitive{A: Vec3{12285, 0, -3}, Radius: 1}
	pos, normal := Vec3{12286, 0, -3}, Vec3{1, 0, 0}
	offset := gpuVec(offsetRayOrigin(pos, normal))
	if offset.X <= pos.X || intersect(p, offset, normal) != noHit {
		t.Fatal("origin bias vanished in float32 at large coordinates")
	}
}

func TestProceduralLargeScene(t *testing.T) {
	source, err := os.ReadFile("many_spheres.ing")
	if err != nil {
		t.Fatal(err)
	}
	scene, err := LoadScene(string(source))
	if err != nil {
		t.Fatal(err)
	}
	frame, err := Snapshot(scene, Options{Width: 17, Height: 9, Samples: 1})
	if err != nil {
		t.Fatal(err)
	}
	if len(frame.primitives) != 1026 || len(frame.nodes) != 2051 {
		t.Fatal("procedural scene size", len(frame.primitives), len(frame.nodes))
	}
}

func BenchmarkBVHClosestHit(b *testing.B) {
	s := DefaultScene()
	m := Material{Albedo: Vec3{1, 1, 1}}
	for i := 0; i < MaxPrimitives; i++ {
		s.AddSphere(Sphere{Vec3{float64(i%64) * 3, float64(i/64) * 3, -3}, 1}, m)
	}
	f, err := Snapshot(s, Options{Width: 1, Height: 1, Samples: 1})
	if err != nil {
		b.Fatal(err)
	}
	b.Run("BVH", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			f.closestHit(Vec3{}, Vec3{0, 0, -1})
		}
	})
	b.Run("Brute", func(b *testing.B) {
		for i := 0; i < b.N; i++ {
			bruteHit(f, Vec3{}, Vec3{0, 0, -1})
		}
	})
}
