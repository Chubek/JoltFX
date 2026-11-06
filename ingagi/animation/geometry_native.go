//go:build cgo

package animation

/*
#cgo CXXFLAGS: -std=c++17 -I${SRCDIR}/../../third_party/lib2geom/include
#cgo LDFLAGS: -lstdc++ -lm
#include "geometry_bridge.h"
*/
import "C"

func bezier(p []Point, t float64) Point {
	var xy [8]C.double
	for i, v := range p {
		xy[2*i] = C.double(v.X)
		xy[2*i+1] = C.double(v.Y)
	}
	var out [2]C.double
	C.ig_anim_bezier(&xy[0], C.uint(len(p)-1), C.double(t), &out[0])
	return Point{float64(out[0]), float64(out[1])}
}
