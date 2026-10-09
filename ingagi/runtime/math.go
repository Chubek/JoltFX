// Math for the Ingagi VM uses float64 vectors, column-major matrices and
// (x,y,z,w) quaternions. CGO builds use vendored GLM for matrix operations
// and simdette for batch addition/scaling; non-CGO builds use Go fallbacks.
// Scalar vector and quaternion operations are implemented in Go.
package runtime

import "math"

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

// Vec2 is a 2D double-precision vector.
type Vec2 struct{ X, Y float64 }

// Vec3 is a 3D double-precision vector.
type Vec3 struct{ X, Y, Z float64 }

// Vec4 is a 4D double-precision vector.
type Vec4 struct{ X, Y, Z, W float64 }

func (a Vec2) Add(b Vec2) Vec2    { return Vec2{a.X + b.X, a.Y + b.Y} }
func (a Vec2) Sub(b Vec2) Vec2    { return Vec2{a.X - b.X, a.Y - b.Y} }
func (a Vec2) Mul(s float64) Vec2 { return Vec2{a.X * s, a.Y * s} }
func (a Vec2) Dot(b Vec2) float64 { return a.X*b.X + a.Y*b.Y }
func (a Vec2) Len() float64       { return math.Sqrt(a.Dot(a)) }
func (a Vec2) Norm() Vec2 {
	if l := a.Len(); l != 0 {
		return a.Mul(1 / l)
	}
	return a
}

func (a Vec3) Add(b Vec3) Vec3    { return Vec3{a.X + b.X, a.Y + b.Y, a.Z + b.Z} }
func (a Vec3) Sub(b Vec3) Vec3    { return Vec3{a.X - b.X, a.Y - b.Y, a.Z - b.Z} }
func (a Vec3) Mul(s float64) Vec3 { return Vec3{a.X * s, a.Y * s, a.Z * s} }
func (a Vec3) Dot(b Vec3) float64 { return a.X*b.X + a.Y*b.Y + a.Z*b.Z }
func (a Vec3) Cross(b Vec3) Vec3 {
	return Vec3{
		a.Y*b.Z - a.Z*b.Y,
		a.Z*b.X - a.X*b.Z,
		a.X*b.Y - a.Y*b.X,
	}
}
func (a Vec3) Len() float64 { return math.Sqrt(a.Dot(a)) }
func (a Vec3) Norm() Vec3 {
	if l := a.Len(); l != 0 {
		return a.Mul(1 / l)
	}
	return a
}

func (a Vec4) Add(b Vec4) Vec4 {
	return Vec4{a.X + b.X, a.Y + b.Y, a.Z + b.Z, a.W + b.W}
}
func (a Vec4) Mul(s float64) Vec4 { return Vec4{a.X * s, a.Y * s, a.Z * s, a.W * s} }
func (a Vec4) Dot(b Vec4) float64 { return a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W }

// ---------------------------------------------------------------------------
// Mat4: column-major 4x4 (glm::mat4 layout, M[col*4+row])
// ---------------------------------------------------------------------------

// Mat4 is a column-major 4x4 float matrix.
type Mat4 [16]float64

// At returns element (col, row).
func (m Mat4) At(col, row int) float64 { return m[col*4+row] }

// Set writes element (col, row).
func (m *Mat4) Set(col, row int, v float64) { m[col*4+row] = v }

// IdentityMat4 returns the identity matrix.
func IdentityMat4() Mat4 {
	return Mat4{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}
}

// Mul returns a*b (ordinary matrix product, glm::operator* order).
func (a Mat4) Mul(b Mat4) Mat4 {
	return multiplyMat4(a, b)
}

func scalarMultiplyMat4(a, b Mat4) Mat4 {
	var o Mat4
	for c := 0; c < 4; c++ {
		for r := 0; r < 4; r++ {
			s := 0.0
			for k := 0; k < 4; k++ {
				s += a.At(k, r) * b.At(c, k)
			}
			o.Set(c, r, s)
		}
	}
	return o
}

// MulVec4 transforms a column vector.
func (m Mat4) MulVec4(v Vec4) Vec4 {
	return Vec4{
		m.At(0, 0)*v.X + m.At(1, 0)*v.Y + m.At(2, 0)*v.Z + m.At(3, 0)*v.W,
		m.At(0, 1)*v.X + m.At(1, 1)*v.Y + m.At(2, 1)*v.Z + m.At(3, 1)*v.W,
		m.At(0, 2)*v.X + m.At(1, 2)*v.Y + m.At(2, 2)*v.Z + m.At(3, 2)*v.W,
		m.At(0, 3)*v.X + m.At(1, 3)*v.Y + m.At(2, 3)*v.Z + m.At(3, 3)*v.W,
	}
}

// Translate returns m * translation (glm::translate form).
func (m Mat4) Translate(v Vec3) Mat4 {
	t := IdentityMat4()
	t.Set(3, 0, v.X)
	t.Set(3, 1, v.Y)
	t.Set(3, 2, v.Z)
	return m.Mul(t)
}

// Scale returns m * scale (glm::scale form).
func (m Mat4) Scale(v Vec3) Mat4 {
	s := Mat4{}
	s.Set(0, 0, v.X)
	s.Set(1, 1, v.Y)
	s.Set(2, 2, v.Z)
	s.Set(3, 3, 1)
	return m.Mul(s)
}

// Rotate returns m * rotation(angle radians, axis) (glm::rotate form).
func (m Mat4) Rotate(angle float64, axis Vec3) Mat4 {
	return m.Mul(AxisAngle(axis.Norm(), angle))
}

// AxisAngle builds a rotation matrix (glm::rotate form).
func AxisAngle(axis Vec3, angle float64) Mat4 {
	c, s := math.Cos(angle), math.Sin(angle)
	t := 1 - c
	x, y, z := axis.X, axis.Y, axis.Z
	var m Mat4
	m.Set(0, 0, t*x*x+c)
	m.Set(0, 1, t*x*y+s*z)
	m.Set(0, 2, t*x*z-s*y)
	m.Set(1, 0, t*x*y-s*z)
	m.Set(1, 1, t*y*y+c)
	m.Set(1, 2, t*y*z+s*x)
	m.Set(2, 0, t*x*z+s*y)
	m.Set(2, 1, t*y*z-s*x)
	m.Set(2, 2, t*z*z+c)
	m.Set(3, 3, 1)
	return m
}

// Perspective builds a projection matrix (glm::perspective form, right-handed,
// depth range [-1, 1] like OpenGL).
func scalarPerspective(fovy, aspect, near, far float64) Mat4 {
	f := 1 / math.Tan(fovy/2)
	var m Mat4
	m.Set(0, 0, f/aspect)
	m.Set(1, 1, f)
	m.Set(2, 2, (far+near)/(near-far))
	m.Set(2, 3, -1)
	m.Set(3, 2, 2*far*near/(near-far))
	return m
}

// LookAt builds a view matrix (glm::lookAt form).
func scalarLookAt(eye, center, up Vec3) Mat4 {
	f := center.Sub(eye).Norm()
	s := f.Cross(up).Norm()
	u := s.Cross(f)
	var m Mat4
	m.Set(0, 0, s.X)
	m.Set(0, 1, u.X)
	m.Set(0, 2, -f.X)
	m.Set(1, 0, s.Y)
	m.Set(1, 1, u.Y)
	m.Set(1, 2, -f.Y)
	m.Set(2, 0, s.Z)
	m.Set(2, 1, u.Z)
	m.Set(2, 2, -f.Z)
	m.Set(3, 0, -s.Dot(eye))
	m.Set(3, 1, -u.Dot(eye))
	m.Set(3, 2, f.Dot(eye))
	m.Set(3, 3, 1)
	return m
}

// ---------------------------------------------------------------------------
// Quaternions (glm::quat layout: x, y, z, w)
// ---------------------------------------------------------------------------

// Quat is a quaternion stored x,y,z,w.
type Quat struct{ X, Y, Z, W float64 }

// IdentityQuat returns the identity rotation.
func IdentityQuat() Quat { return Quat{0, 0, 0, 1} }

// AngleAxis builds a rotation quaternion (glm::angleAxis form).
func AngleAxis(angle float64, axis Vec3) Quat {
	n := axis.Norm()
	s := math.Sin(angle / 2)
	return Quat{n.X * s, n.Y * s, n.Z * s, math.Cos(angle / 2)}
}

// Mul composes rotations (glm::operator* order: a applied after b as a*b).
func (a Quat) Mul(b Quat) Quat {
	return Quat{
		a.W*b.X + a.X*b.W + a.Y*b.Z - a.Z*b.Y,
		a.W*b.Y - a.X*b.Z + a.Y*b.W + a.Z*b.X,
		a.W*b.Z + a.X*b.Y - a.Y*b.X + a.Z*b.W,
		a.W*b.W - a.X*b.X - a.Y*b.Y - a.Z*b.Z,
	}
}

// RotateVec rotates v by q.
func (q Quat) RotateVec(v Vec3) Vec3 {
	// q * (v,0) * conj(q), expanded without temporaries.
	x, y, z, w := q.X, q.Y, q.Z, q.W
	ix := w*v.X + y*v.Z - z*v.Y
	iy := w*v.Y + z*v.X - x*v.Z
	iz := w*v.Z + x*v.Y - y*v.X
	iw := -x*v.X - y*v.Y - z*v.Z
	return Vec3{
		ix*w + iw*-x + iy*-z - iz*-y,
		iy*w + iw*-y + iz*-x - ix*-z,
		iz*w + iw*-z + ix*-y - iy*-x,
	}
}

// Norm normalizes q.
func (q Quat) Norm() Quat {
	l := math.Sqrt(q.X*q.X + q.Y*q.Y + q.Z*q.Z + q.W*q.W)
	if l == 0 {
		return IdentityQuat()
	}
	return Quat{q.X / l, q.Y / l, q.Z / l, q.W / l}
}

// Slerp interpolates (glm::slerp form).
func (a Quat) Slerp(b Quat, t float64) Quat {
	dot := a.X*b.X + a.Y*b.Y + a.Z*b.Z + a.W*b.W
	if dot < 0 {
		b = Quat{-b.X, -b.Y, -b.Z, -b.W}
		dot = -dot
	}
	if dot > 0.9995 {
		return Quat{
			a.X + t*(b.X-a.X), a.Y + t*(b.Y-a.Y),
			a.Z + t*(b.Z-a.Z), a.W + t*(b.W-a.W),
		}.Norm()
	}
	th := math.Acos(dot)
	s := math.Sin(th)
	return Quat{
		(a.X*math.Sin((1-t)*th) + b.X*math.Sin(t*th)) / s,
		(a.Y*math.Sin((1-t)*th) + b.Y*math.Sin(t*th)) / s,
		(a.Z*math.Sin((1-t)*th) + b.Z*math.Sin(t*th)) / s,
		(a.W*math.Sin((1-t)*th) + b.W*math.Sin(t*th)) / s,
	}
}

// ToMat4 converts to a rotation matrix.
func (q Quat) ToMat4() Mat4 {
	x, y, z, w := q.X, q.Y, q.Z, q.W
	var m Mat4
	m.Set(0, 0, 1-2*(y*y+z*z))
	m.Set(0, 1, 2*(x*y+z*w))
	m.Set(0, 2, 2*(x*z-y*w))
	m.Set(1, 0, 2*(x*y-z*w))
	m.Set(1, 1, 1-2*(x*x+z*z))
	m.Set(1, 2, 2*(y*z+x*w))
	m.Set(2, 0, 2*(x*z+y*w))
	m.Set(2, 1, 2*(y*z-x*w))
	m.Set(2, 2, 1-2*(x*x+y*y))
	m.Set(3, 3, 1)
	return m
}

// ---------------------------------------------------------------------------
// Portable reference implementations for flat [x,y,z]*n batch operations.
// ---------------------------------------------------------------------------

const batchWidth = 4

// AddVec3Batch computes dst[i] = a[i] + b[i] over flat [x,y,z]*n triples.
func scalarAddVec3Batch(dst, a, b []float64) {
	n := len(a) / 3
	if len(b)/3 < n {
		n = len(b) / 3
	}
	if len(dst)/3 < n {
		n = len(dst) / 3
	}
	i := 0
	for ; i+batchWidth <= n; i += batchWidth {
		for k := 0; k < batchWidth; k++ {
			j := (i + k) * 3
			dst[j], dst[j+1], dst[j+2] = a[j]+b[j], a[j+1]+b[j+1], a[j+2]+b[j+2]
		}
	}
	for ; i < n; i++ { // scalar tail
		j := i * 3
		dst[j], dst[j+1], dst[j+2] = a[j]+b[j], a[j+1]+b[j+1], a[j+2]+b[j+2]
	}
}

// ScaleVec3Batch computes dst[i] = a[i] * s.
func scalarScaleVec3Batch(dst, a []float64, s float64) {
	n := len(a) / 3
	if len(dst)/3 < n {
		n = len(dst) / 3
	}
	i := 0
	for ; i+batchWidth <= n; i += batchWidth {
		for k := 0; k < batchWidth; k++ {
			j := (i + k) * 3
			dst[j], dst[j+1], dst[j+2] = a[j]*s, a[j+1]*s, a[j+2]*s
		}
	}
	for ; i < n; i++ { // scalar tail
		j := i * 3
		dst[j], dst[j+1], dst[j+2] = a[j]*s, a[j+1]*s, a[j+2]*s
	}
}

// TransformPoints applies m to flat [x,y,z]*n points with w=1.
func scalarTransformPoints(dst, src []float64, m Mat4) {
	n := len(src) / 3
	if len(dst)/3 < n {
		n = len(dst) / 3
	}
	i := 0
	for ; i+batchWidth <= n; i += batchWidth {
		for k := 0; k < batchWidth; k++ {
			j := (i + k) * 3
			v := m.MulVec4(Vec4{src[j], src[j+1], src[j+2], 1})
			dst[j], dst[j+1], dst[j+2] = v.X, v.Y, v.Z
		}
	}
	for ; i < n; i++ { // scalar tail
		j := i * 3
		v := m.MulVec4(Vec4{src[j], src[j+1], src[j+2], 1})
		dst[j], dst[j+1], dst[j+2] = v.X, v.Y, v.Z
	}
}

// Lerp performs scalar linear interpolation.
func Lerp(a, b, t float64) float64 { return a + (b-a)*t }

// Clamp restricts v to [lo, hi].
func Clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}
