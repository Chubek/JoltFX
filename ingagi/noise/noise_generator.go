// Package noise implements deterministic, seeded gradient noise and bounded fBm.
// Generators are immutable and may be sampled concurrently without global RNGs.
package noise

import (
	"fmt"
	"math"
)

// CoordinateLimit preserves useful fractional precision and safe lattice indices.
const CoordinateLimit = float64(1 << 40)

type Generator struct{ permutation [512]uint8 }

// New constructs a reproducible improved-Perlin lattice with a period of 256.
// Seed expansion uses specified integer operations, independent of math/rand.
func New(seed uint64) Generator {
	var g Generator
	for i := 0; i < 256; i++ {
		g.permutation[i] = uint8(i)
	}
	next := func() uint64 {
		seed += 0x9e3779b97f4a7c15
		z := seed
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9
		z = (z ^ (z >> 27)) * 0x94d049bb133111eb
		return z ^ (z >> 31)
	}
	for i := 255; i > 0; i-- {
		j := int(next() % uint64(i+1))
		g.permutation[i], g.permutation[j] = g.permutation[j], g.permutation[i]
	}
	copy(g.permutation[256:], g.permutation[:256])
	return g
}

func validCoordinate(v float64) bool {
	return !math.IsNaN(v) && !math.IsInf(v, 0) && math.Abs(v) <= CoordinateLimit
}
func fade(v float64) float64      { return v * v * v * (v*(v*6-15) + 10) }
func mix(a, b, t float64) float64 { return a + (b-a)*t }
func gradient(h uint8, x, y, z float64) float64 {
	h &= 15
	u := y
	if h < 8 {
		u = x
	}
	v := z
	if h < 4 {
		v = y
	} else if h == 12 || h == 14 {
		v = x
	}
	if h&1 != 0 {
		u = -u
	}
	if h&2 != 0 {
		v = -v
	}
	return u + v
}

func (g Generator) Sample2(x, y float64) (float64, error) { return g.Sample3(x, y, 0) }

// Sample3 returns signed noise in [-1,1]. Invalid or excessively large
// coordinates return an error, never an implementation-dependent integer cast.
func (g Generator) Sample3(x, y, z float64) (float64, error) {
	if !validCoordinate(x) || !validCoordinate(y) || !validCoordinate(z) {
		return 0, fmt.Errorf("noise: coordinates must be finite and within +/-2^40")
	}
	fx, fy, fz := math.Floor(x), math.Floor(y), math.Floor(z)
	x -= fx
	y -= fy
	z -= fz
	xi, yi, zi := int(int64(fx)&255), int(int64(fy)&255), int(int64(fz)&255)
	u, v, w := fade(x), fade(y), fade(z)
	p := &g.permutation
	hash := func(dx, dy, dz int) uint8 { return p[int(p[int(p[xi+dx])+yi+dy])+zi+dz] }
	a := mix(gradient(hash(0, 0, 0), x, y, z), gradient(hash(1, 0, 0), x-1, y, z), u)
	b := mix(gradient(hash(0, 1, 0), x, y-1, z), gradient(hash(1, 1, 0), x-1, y-1, z), u)
	c := mix(gradient(hash(0, 0, 1), x, y, z-1), gradient(hash(1, 0, 1), x-1, y, z-1), u)
	d := mix(gradient(hash(0, 1, 1), x, y-1, z-1), gradient(hash(1, 1, 1), x-1, y-1, z-1), u)
	return mix(mix(a, b, v), mix(c, d, v), w) * .5, nil
}
