package runtime

import (
	"fmt"
	"math"

	anim "github.com/Chubek/JoltFX/ingagi/animation"
)

func registerAnimation(vm *VM) {
	for name, sig := range anim.Signatures {
		name, sig := name, sig
		vm.builtins[name] = func(vm *VM, a []Value) (Value, error) {
			if len(a) != len(sig.Args) {
				return NilValue, fmt.Errorf("%s expects %d arguments", name, len(sig.Args))
			}
			// Validate scalar kinds and finite values before any indexing or conversion.
			for i, kind := range sig.Args {
				v := a[i]
				ok := true
				switch kind {
				case "float":
					_, err := proceduralNumber(v)
					ok = err == nil
				case "int":
					ok = v.K == KInt
				case "bool":
					ok = v.K == KBool
				case "string":
					ok = v.K == KString
				case "vec2":
					ok = v.K == KVec2 && !math.IsNaN(v.V[0]) && !math.IsNaN(v.V[1]) && !math.IsInf(v.V[0], 0) && !math.IsInf(v.V[1], 0)
				}
				if !ok {
					return NilValue, fmt.Errorf("%s: argument %d must be finite %s", name, i+1, kind)
				}
			}
			return animationCall(name, a)
		}
	}
}
func animArray(v Value, max int) ([]Value, error) {
	p, ok := v.O.(*ArrayObj)
	if v.K != KArray || !ok || p == nil || len(p.Elems) > max {
		return nil, fmt.Errorf("animation: invalid or oversized array")
	}
	return p.Elems, nil
}
func animPoints(v Value) ([]anim.Point, error) {
	a, err := animArray(v, anim.MaxPoints)
	if err != nil {
		return nil, err
	}
	p := make([]anim.Point, len(a))
	for i, v := range a {
		if v.K != KVec2 {
			return nil, fmt.Errorf("animation: expected vec2 point")
		}
		p[i] = anim.Point{X: v.V[0], Y: v.V[1]}
	}
	return p, anim.ValidatePoints(p)
}
func animNumbers(v Value) ([]float64, error) {
	a, err := animArray(v, anim.MaxPoints)
	if err != nil {
		return nil, err
	}
	p := make([]float64, len(a))
	for i, v := range a {
		p[i], err = proceduralNumber(v)
		if err != nil {
			return nil, err
		}
	}
	return p, nil
}
func animPointValue(p anim.Point) Value { return Value{K: KVec2, V: [4]float64{p.X, p.Y}} }
func animPointValues(p []anim.Point, err error) (Value, error) {
	if err != nil {
		return NilValue, err
	}
	a := make([]Value, len(p))
	for i, v := range p {
		a[i] = animPointValue(v)
	}
	return Value{K: KArray, O: &ArrayObj{Elems: a}}, nil
}
func animColor(v Value) (uint32, error) {
	if v.K != KInt || v.I < 0 || v.I > math.MaxUint32 {
		return 0, fmt.Errorf("animation: color must be a packed RGBA u32")
	}
	return uint32(v.I), nil
}
func animShapeValue(s anim.Shape) (Value, error) {
	if err := s.Validate(); err != nil {
		return NilValue, err
	}
	p, _ := animPointValues(s.Points, nil)
	return Value{K: KMap, O: &MapObj{Fields: map[string]Value{"kind": IntValue(int64(s.Kind)), "points": p, "fill": IntValue(int64(s.Fill)), "stroke": IntValue(int64(s.Stroke)), "width": FloatValue(s.Width), "layer": IntValue(int64(s.Layer))}}}, nil
}
func animShape(v Value) (anim.Shape, error) {
	m, ok := v.O.(*MapObj)
	if v.K != KMap || !ok || m == nil {
		return anim.Shape{}, fmt.Errorf("animation: expected shape map")
	}
	f := m.Fields
	s := anim.Shape{}
	k, l := f["kind"], f["layer"]
	if k.K != KInt || k.I < 0 || k.I > 4 || l.K != KInt || l.I < 0 || l.I > 7 {
		return s, fmt.Errorf("animation: invalid kind/layer")
	}
	s.Kind, s.Layer = int(k.I), int(l.I)
	var err error
	if s.Points, err = animPoints(f["points"]); err != nil {
		return s, err
	}
	if s.Fill, err = animColor(f["fill"]); err != nil {
		return s, err
	}
	if s.Stroke, err = animColor(f["stroke"]); err != nil {
		return s, err
	}
	if s.Width, err = proceduralNumber(f["width"]); err != nil {
		return s, err
	}
	return s, s.Validate()
}

// AnimationFrame detaches and validates script values at the authoring boundary.
func AnimationFrame(v Value) ([]anim.Shape, error) {
	a, err := animArray(v, 8*anim.MaxShapes)
	if err != nil {
		return nil, err
	}
	out := make([]anim.Shape, len(a))
	var counts [8]int
	for i, v := range a {
		out[i], err = animShape(v)
		if err != nil {
			return nil, err
		}
		counts[out[i].Layer]++
		if counts[out[i].Layer] > anim.MaxShapes {
			return nil, fmt.Errorf("animation: layer shape limit exceeded")
		}
	}
	return out, nil
}
func animationCall(name string, a []Value) (Value, error) {
	num := func(i int) float64 { v, _ := a[i].AsFloat(); return v }
	point := func(i int) anim.Point { return anim.Point{X: a[i].V[0], Y: a[i].V[1]} }
	scalar := func(v float64, err error) (Value, error) {
		if err != nil {
			return NilValue, err
		}
		if math.IsNaN(v) || math.IsInf(v, 0) {
			return NilValue, fmt.Errorf("animation: numeric overflow")
		}
		return FloatValue(v), nil
	}
	switch name {
	case "anim_rgba":
		var c uint32
		for i := 0; i < 4; i++ {
			v := num(i)
			if v < 0 || v > 1 {
				return NilValue, fmt.Errorf("animation: RGBA channels must be 0..1")
			}
			c |= uint32(math.Round(v*255)) << uint(i*8)
		}
		return IntValue(int64(c)), nil
	case "anim_ease":
		return scalar(anim.Ease(a[0].S, num(1)))
	case "anim_tween":
		u, err := anim.Ease(a[3].S, num(2))
		return scalar(num(0)*(1-u)+num(1)*u, err)
	case "anim_track":
		times, err := animNumbers(a[0])
		if err != nil {
			return NilValue, err
		}
		values, err := animNumbers(a[1])
		if err != nil {
			return NilValue, err
		}
		return scalar(anim.Track(times, values, num(2), a[3].S))
	case "anim_loop", "anim_pingpong":
		d := num(1)
		if d <= 0 {
			return NilValue, fmt.Errorf("animation: duration must be positive")
		}
		u := num(0) / d
		if math.IsInf(u, 0) {
			return NilValue, fmt.Errorf("animation: time overflow")
		}
		if name == "anim_loop" {
			return scalar(u-math.Floor(u), nil)
		}
		u = math.Mod(u, 2)
		if u < 0 {
			u += 2
		}
		return scalar(1-math.Abs(u-1), nil)
	case "anim_rect", "anim_ellipse", "anim_line", "anim_path":
		s := anim.Shape{Kind: 2}
		fillIndex, strokeIndex := 2, 3
		if name != "anim_line" {
			s.Width = num(4)
		}
		if name == "anim_line" {
			s.Kind = 1
			s.Width = num(3)
			fillIndex = -1
			strokeIndex = 2
		}
		if name == "anim_ellipse" {
			s.Kind = 3
		}
		if name == "anim_path" {
			var err error
			s.Points, err = animPoints(a[0])
			if err != nil {
				return NilValue, err
			}
			s.Kind = 0
			if a[1].B {
				s.Kind = 4
			}
		} else {
			s.Points = []anim.Point{point(0), point(1)}
		}
		var err error
		if fillIndex >= 0 {
			s.Fill, err = animColor(a[fillIndex])
			if err != nil {
				return NilValue, err
			}
		}
		s.Stroke, err = animColor(a[strokeIndex])
		if err != nil {
			return NilValue, err
		}
		return animShapeValue(s)
	case "anim_opacity":
		s, err := animShape(a[0])
		if err != nil {
			return NilValue, err
		}
		u := num(1)
		if u < 0 || u > 1 {
			return NilValue, fmt.Errorf("animation: opacity must be 0..1")
		}
		fade := func(c uint32) uint32 { return c&0xffffff | uint32(math.Round(float64(c>>24)*u))<<24 }
		s.Fill = fade(s.Fill)
		s.Stroke = fade(s.Stroke)
		return animShapeValue(s)
	case "anim_layer":
		shapes, err := AnimationFrame(a[0])
		if err != nil {
			return NilValue, err
		}
		if a[1].I < 0 || a[1].I > 7 || len(shapes) > anim.MaxShapes {
			return NilValue, fmt.Errorf("animation: invalid layer/count")
		}
		out := make([]Value, len(shapes))
		for i, s := range shapes {
			s.Layer = int(a[1].I)
			out[i], _ = animShapeValue(s)
		}
		return Value{K: KArray, O: &ArrayObj{Elems: out}}, nil
	case "anim_arc", "anim_star":
		stepsIndex := 4
		if name == "anim_star" {
			stepsIndex = 3
		}
		n := a[stepsIndex].I
		if n < 2 || n > int64((anim.MaxPoints-1)/2) {
			return NilValue, fmt.Errorf("animation: tessellation out of range")
		}
		c := point(0)
		p := []anim.Point{}
		if name == "anim_arc" {
			r := point(1)
			if r.X < 0 || r.Y < 0 {
				return NilValue, fmt.Errorf("animation: negative radius")
			}
			p = make([]anim.Point, int(n)+1)
			for i := range p {
				u := float64(i) / float64(n)
				theta := num(2)*(1-u) + num(3)*u
				p[i] = anim.Point{X: c.X + r.X*math.Cos(theta), Y: c.Y + r.Y*math.Sin(theta)}
			}
		} else {
			if num(1) <= 0 || num(2) <= 0 {
				return NilValue, fmt.Errorf("animation: radii must be positive")
			}
			p = make([]anim.Point, int(n)*2)
			for i := range p {
				r := num(1)
				if i%2 != 0 {
					r = num(2)
				}
				theta := num(4) + float64(i)*math.Pi/float64(n)
				p[i] = anim.Point{X: c.X + r*math.Cos(theta), Y: c.Y + r*math.Sin(theta)}
			}
		}
		return animPointValues(p, anim.ValidatePoints(p))
	}
	p, err := animPoints(a[0])
	if err != nil {
		return NilValue, err
	}
	switch name {
	case "anim_bezier":
		v, err := anim.Bezier(p, num(1))
		return animPointValue(v), err
	case "anim_curve":
		if a[1].I < 1 || a[1].I >= anim.MaxPoints {
			return NilValue, fmt.Errorf("animation: curve subdivisions out of range")
		}
		return animPointValues(anim.Curve(p, int(a[1].I)))
	case "anim_length":
		return scalar(anim.Length(p))
	case "anim_along":
		v, err := anim.Along(p, num(1))
		return animPointValue(v), err
	case "anim_morph":
		q, err := animPoints(a[1])
		if err != nil {
			return NilValue, err
		}
		return animPointValues(anim.Morph(p, q, num(2)))
	case "anim_transform":
		m, err := animNumbers(a[1])
		if err != nil {
			return NilValue, err
		}
		if len(m) != 6 {
			return NilValue, fmt.Errorf("animation: affine matrix needs six numbers")
		}
		return animPointValues(anim.Transform(p, [6]float64{m[0], m[1], m[2], m[3], m[4], m[5]}))
	case "anim_translate":
		q := point(1)
		return animPointValues(anim.Transform(p, [6]float64{1, 0, 0, 1, q.X, q.Y}))
	case "anim_rotate":
		c, s := math.Cos(num(1)), math.Sin(num(1))
		q := point(2)
		return animPointValues(anim.Transform(p, [6]float64{c, s, -s, c, q.X - c*q.X + s*q.Y, q.Y - s*q.X - c*q.Y}))
	case "anim_scale":
		s, q := point(1), point(2)
		return animPointValues(anim.Transform(p, [6]float64{s.X, 0, 0, s.Y, q.X * (1 - s.X), q.Y * (1 - s.Y)}))
	}
	return NilValue, fmt.Errorf("animation: unknown operation %s", name)
}
