//go:build !cgo

package animation

// Matches lib2geom's Bernstein evaluation order for degree 1..3.
func bezier(p []Point, t float64) Point {
	n := len(p) - 1
	u, bc, tn := 1-t, 1.0, 1.0
	r := Point{p[0].X * u, p[0].Y * u}
	for i := 1; i < n; i++ {
		tn *= t
		bc = bc * float64(n-i+1) / float64(i)
		r = Point{(r.X + tn*bc*p[i].X) * u, (r.Y + tn*bc*p[i].Y) * u}
	}
	return Point{r.X + tn*t*p[n].X, r.Y + tn*t*p[n].Y}
}
