package runtime

import (
	"math"
	"math/rand"
	"testing"
)

// Conformance vectors for glm/simdette-compatible math. Values follow
// glm::perspective / glm::lookAt / quaternion conventions exactly.

func TestPerspective(t *testing.T) {
	m := Perspective(math.Pi/4, 16.0/9.0, 0.1, 100.0)
	f := 1 / math.Tan(math.Pi/8)
	if !close(m.At(0, 0), f/(16.0/9.0)) || !close(m.At(1, 1), f) {
		t.Fatalf("perspective diagonal: %v %v", m.At(0, 0), m.At(1, 1))
	}
	if m.At(2, 3) != -1 || !close(m.At(3, 2), 2*100*0.1/(0.1-100)) {
		t.Fatalf("perspective depth row: %v %v", m.At(2, 3), m.At(3, 2))
	}
}

func TestLookAt(t *testing.T) {
	m := LookAt(Vec3{0, 0, 5}, Vec3{0, 0, 0}, Vec3{0, 1, 0})
	// Camera at +z looking to origin: translation z = -5 in row 2.
	if !close(m.At(3, 2), -5) {
		t.Fatalf("lookat tz: %v", m.At(3, 2))
	}
	if !close(m.At(0, 0), 1) || !close(m.At(1, 1), 1) || !close(m.At(2, 2), 1) {
		t.Fatalf("lookat rotation: %v", m)
	}
}

func TestQuatRotate(t *testing.T) {
	q := AngleAxis(math.Pi/2, Vec3{0, 0, 1})
	r := q.Norm().RotateVec(Vec3{1, 0, 0})
	if !close(r.X, 0) || !close(r.Y, 1) || !close(r.Z, 0) {
		t.Fatalf("quat rotate: %+v", r)
	}
}

func TestMatMulIdentity(t *testing.T) {
	a := AxisAngle(Vec3{0, 1, 0}, 0.7)
	id := IdentityMat4()
	if a.Mul(id) != a || id.Mul(a) != a {
		t.Fatal("identity law broken")
	}
}

func TestBatchHelpers(t *testing.T) {
	a := []float64{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13}
	b := []float64{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}
	dst := make([]float64, 13)
	AddVec3Batch(dst, a, b)
	for i := 0; i < 12; i++ {
		if dst[i] != a[i]+1 {
			t.Fatalf("batch[%d] = %v", i, dst[i])
		}
	}
	ScaleVec3Batch(dst, a, 2)
	for i := 0; i < 12; i++ {
		if dst[i] != a[i]*2 {
			t.Fatalf("scale[%d] = %v", i, dst[i])
		}
	}
	// Odd length exercises the scalar tail (13 floats = 4 triples + 1).
	TransformPoints(dst, a, IdentityMat4())
	for i := 0; i < 12; i++ {
		if dst[i] != a[i] {
			t.Fatalf("xform[%d] = %v", i, dst[i])
		}
	}
}

func close(a, b float64) bool { return math.Abs(a-b) < 1e-9 }

func TestMathBackendAgainstReference(t *testing.T) {
	rng := rand.New(rand.NewSource(42))
	for iteration := 0; iteration < 50; iteration++ {
		var a, b Mat4
		for i := range a {
			a[i], b[i] = rng.Float64()*20-10, rng.Float64()*20-10
		}
		got, want := a.Mul(b), scalarMultiplyMat4(a, b)
		for i := range got {
			if !close(got[i], want[i]) {
				t.Fatalf("matrix[%d]: got %g, want %g", i, got[i], want[i])
			}
		}
		// Include non-axis-aligned camera bases and non-identity transforms.
		eye := Vec3{rng.Float64() + 1, rng.Float64() + 2, rng.Float64() + 3}
		got, want = LookAt(eye, Vec3{}, Vec3{0, 1, 0}), scalarLookAt(eye, Vec3{}, Vec3{0, 1, 0})
		for i := range got {
			if !close(got[i], want[i]) {
				t.Fatalf("lookAt[%d]: got %g, want %g", i, got[i], want[i])
			}
		}
		for n := 0; n < 32; n++ {
			x, y := make([]float64, n+2), make([]float64, n+1)
			for i := range x {
				x[i] = rng.Float64()
			}
			for i := range y {
				y[i] = rng.Float64()
			}
			out, ref := make([]float64, n), make([]float64, n)
			AddVec3Batch(out, x, y)
			scalarAddVec3Batch(ref, x, y)
			checkMathSlices(t, out, ref)
			ScaleVec3Batch(out, out, 1.3)
			scalarScaleVec3Batch(ref, ref, 1.3)
			checkMathSlices(t, out, ref)
			TransformPoints(out, out, a)
			scalarTransformPoints(ref, ref, a)
			checkMathSlices(t, out, ref)
			AddVec3Batch(out, out, out)
			scalarAddVec3Batch(ref, ref, ref)
			checkMathSlices(t, out, ref)
		}
	}
}

func checkMathSlices(t *testing.T, got, want []float64) {
	t.Helper()
	for i := range got {
		if !close(got[i], want[i]) {
			t.Fatalf("batch[%d]: got %g, want %g", i, got[i], want[i])
		}
	}
}
