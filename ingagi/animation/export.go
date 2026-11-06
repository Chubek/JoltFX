package animation

import (
	"bufio"
	_ "embed"
	"encoding/json"
	"fmt"
	"io"
	"sort"
	"strings"
)

//go:embed canvas.js
var canvasPlayer string

func (d Document) HTML() (string, error) {
	if err := d.Validate(); err != nil {
		return "", err
	}
	// JSON's default HTML escaping prevents script-element termination.
	data, err := json.Marshal(d)
	if err != nil {
		return "", err
	}
	return "<!doctype html><meta charset=\"utf-8\"><title>Ingagi 2D Animation</title>\n<canvas id=\"animation\" width=\"960\" height=\"540\" aria-label=\"Animation\"></canvas>\n<button id=\"play\">Pause</button><input id=\"seek\" type=\"range\" min=\"0\" step=\"1\" value=\"0\"><output id=\"frame\"></output>\n<script>\nconst animation=" + string(data) + ";\n" + canvasPlayer + "\n</script>\n", nil
}
func color(c uint32) string {
	return fmt.Sprintf("rgba(%d,%d,%d,%.9g)", c&255, (c>>8)&255, (c>>16)&255, float64(c>>24)/255)
}
func (d Document) SVG(frame int) (string, error) {
	if err := d.Validate(); err != nil {
		return "", err
	}
	if frame < 0 || frame >= len(d.Frames) {
		return "", fmt.Errorf("animation: frame out of range")
	}
	shapes := append([]Shape(nil), d.Frames[frame]...)
	sort.SliceStable(shapes, func(i, j int) bool { return shapes[i].Layer < shapes[j].Layer })
	var b strings.Builder
	b.WriteString("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"540\" viewBox=\"0 0 960 540\">\n")
	for _, s := range shapes {
		a, z := s.Points[0], s.Points[len(s.Points)-1]
		x, y := min(a.X, z.X), min(a.Y, z.Y)
		w, h := max(a.X, z.X)-x, max(a.Y, z.Y)-y
		switch s.Kind {
		case 2:
			fmt.Fprintf(&b, "<rect x=\"%g\" y=\"%g\" width=\"%g\" height=\"%g\"", x, y, w, h)
		case 3:
			fmt.Fprintf(&b, "<ellipse cx=\"%g\" cy=\"%g\" rx=\"%g\" ry=\"%g\"", x+w/2, y+h/2, w/2, h/2)
		default:
			tag := "polyline"
			if s.Kind == 4 {
				tag = "polygon"
			}
			fmt.Fprintf(&b, "<%s points=\"", tag)
			for _, p := range s.Points {
				fmt.Fprintf(&b, "%g,%g ", p.X, p.Y)
			}
			b.WriteString("\"")
		}
		fill := "none"
		if s.Kind >= 2 {
			fill = color(s.Fill)
		}
		fmt.Fprintf(&b, " fill=\"%s\" stroke=\"%s\" stroke-width=\"%g\" stroke-linejoin=\"round\" stroke-linecap=\"round\"/>\n", fill, color(s.Stroke), s.Width)
	}
	b.WriteString("</svg>\n")
	return b.String(), nil
}

// ReadDrawing expands held desktop cels into the same sampled export document.
// Invisible layers consume validation budgets but do not emit shapes.
func ReadDrawing(source string) (Document, error) {
	fail := func() (Document, error) { return Document{}, fmt.Errorf("animation: invalid drawing document") }
	if len(source) > 64<<20 {
		return fail()
	}
	r := bufio.NewReader(strings.NewReader(source))
	var magic string
	var frames, fps, layers int
	if _, e := fmt.Fscan(r, &magic, &frames, &fps, &layers); e != nil || (magic != "JFXDRAW1" && magic != "JFXDRAW2") || frames < 1 || frames > MaxFrames || fps < 1 || fps > 60 || layers < 1 || layers > 8 {
		return fail()
	}
	d := Document{FPS: fps, Frames: make([][]Shape, frames)}
	total := 0
	expanded := 0
	hold := func(shapes []Shape, from, to int) bool {
		points := 0
		for _, s := range shapes {
			points += len(s.Points)
		}
		if (to-from)*points > MaxTotalPoints-expanded {
			return false
		}
		expanded += (to - from) * points
		for f := from; f < to; f++ {
			d.Frames[f] = append(d.Frames[f], shapes...)
		}
		return true
	}
	for l := 0; l < layers; l++ {
		var visible, locked, keys int
		if _, e := fmt.Fscan(r, &visible, &locked, &keys); e != nil || visible < 0 || visible > 1 || locked < 0 || locked > 1 || keys < 1 || keys > frames {
			return fail()
		}
		previous := 0
		var held []Shape
		for k := 0; k < keys; k++ {
			var frame, count int
			if _, e := fmt.Fscan(r, &frame, &count); e != nil || frame < 1 || frame > frames || (k > 0 && frame <= previous+1) || (k == 0 && frame != 1) || count < 0 || count > MaxShapes {
				return fail()
			}
			if visible == 1 {
				if !hold(held, previous, frame-1) {
					return fail()
				}
			}
			held = nil
			for i := 0; i < count; i++ {
				s := Shape{Layer: l}
				var filled, n int
				if _, e := fmt.Fscan(r, &s.Kind, &s.Stroke, &s.Fill, &s.Width, &filled, &n); e != nil || n < 2 || n > MaxPoints || filled < 0 || filled > 1 || (magic == "JFXDRAW1" && s.Kind > 3) {
					return fail()
				}
				total += n
				if total > MaxTotalPoints {
					return fail()
				}
				s.Points = make([]Point, n)
				for j := range s.Points {
					if _, e := fmt.Fscan(r, &s.Points[j].X, &s.Points[j].Y); e != nil {
						return fail()
					}
				}
				if e := s.Validate(); e != nil {
					return fail()
				}
				if filled == 0 {
					s.Fill = 0
				}
				held = append(held, s)
			}
			previous = frame - 1
		}
		if visible == 1 {
			if !hold(held, previous, frames) {
				return fail()
			}
		}
	}
	var extra string
	if _, e := fmt.Fscan(r, &extra); e != io.EOF {
		return fail()
	}
	if e := d.Validate(); e != nil {
		return Document{}, e
	}
	return d, nil
}
