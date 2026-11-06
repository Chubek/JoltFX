package animation

import (
	"fmt"
	"strings"
)

// Shape kinds match the versioned desktop vector document. Polygon is closed;
// open paths ignore Fill. Colors are packed R | G<<8 | B<<16 | A<<24.
type Shape struct {
	Kind   int     `json:"kind"`
	Points []Point `json:"points"`
	Fill   uint32  `json:"fill"`
	Stroke uint32  `json:"stroke"`
	Width  float64 `json:"width"`
	Layer  int     `json:"layer"`
}

func (s Shape) Validate() error {
	if s.Kind < 0 || s.Kind > 4 || s.Layer < 0 || s.Layer > 7 || !finite(s.Width) || s.Width < .5 || s.Width > 40 {
		return fmt.Errorf("animation: invalid shape style/kind/layer")
	}
	if (s.Kind >= 1 && s.Kind <= 3) && len(s.Points) != 2 {
		return fmt.Errorf("animation: primitive needs two points")
	}
	if s.Kind == 4 && len(s.Points) < 3 {
		return fmt.Errorf("animation: polygon needs three points")
	}
	return ValidatePoints(s.Points)
}

type Document struct {
	FPS    int       `json:"fps"`
	Frames [][]Shape `json:"frames"`
}

func (d Document) Validate() error {
	if d.FPS < 1 || d.FPS > 60 || len(d.Frames) < 1 || len(d.Frames) > MaxFrames {
		return fmt.Errorf("animation: fps must be 1..60 and frames 1..240")
	}
	total := 0
	for _, frame := range d.Frames {
		var counts [8]int
		for _, s := range frame {
			if err := s.Validate(); err != nil {
				return err
			}
			counts[s.Layer]++
			if counts[s.Layer] > MaxShapes {
				return fmt.Errorf("animation: shape limit exceeded")
			}
			total += len(s.Points)
			if total > MaxTotalPoints {
				return fmt.Errorf("animation: total point budget exceeded")
			}
		}
	}
	return nil
}

// Drawing serializes all sampled cels, preserving layers and transparent colors.
func (d Document) Drawing() (string, error) {
	if err := d.Validate(); err != nil {
		return "", err
	}
	layers := 1
	for _, f := range d.Frames {
		for _, s := range f {
			if s.Layer >= layers {
				layers = s.Layer + 1
			}
		}
	}
	var b strings.Builder
	fmt.Fprintf(&b, "JFXDRAW2 %d %d %d\n", len(d.Frames), d.FPS, layers)
	for l := 0; l < layers; l++ {
		fmt.Fprintf(&b, "1 0 %d\n", len(d.Frames))
		for i, f := range d.Frames {
			n := 0
			for _, s := range f {
				if s.Layer == l {
					n++
				}
			}
			fmt.Fprintf(&b, "%d %d\n", i+1, n)
			for _, s := range f {
				if s.Layer != l {
					continue
				}
				filled := 0
				if s.Fill>>24 != 0 {
					filled = 1
				}
				fmt.Fprintf(&b, "%d %d %d %.9g %d %d\n", s.Kind, s.Stroke, s.Fill, s.Width, filled, len(s.Points))
				for _, p := range s.Points {
					fmt.Fprintf(&b, "%.9g %.9g\n", p.X, p.Y)
				}
			}
		}
	}
	return b.String(), nil
}
