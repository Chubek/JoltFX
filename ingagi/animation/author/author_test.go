package author

import (
	"github.com/Chubek/JoltFX/ingagi/animation"
	"os"
	"strings"
	"testing"
)

func TestBakeExport(t *testing.T) {
	src := `fn frame(t: f64) {return [anim_ellipse(vec2(10+t*24,20),vec2(40+t*24,50),anim_rgba(1,0,0,0.5),0,1)];}`
	d, err := Bake(src, 24, 3)
	if err != nil {
		t.Fatal(err)
	}
	if d.Frames[2][0].Points[0].X != 12 {
		t.Fatal(d)
	}
	text, err := d.Drawing()
	if err != nil {
		t.Fatal(err)
	}
	back, err := animation.ReadDrawing(text)
	if err != nil || back.Frames[2][0].Points[0].X != 12 {
		t.Fatal(back, err)
	}
	html, err := d.HTML()
	if err != nil || !strings.Contains(html, "ctx.ellipse") || !strings.Contains(html, "requestAnimationFrame") {
		t.Fatal(err)
	}
	svg, err := d.SVG(2)
	if err != nil || !strings.Contains(svg, `cx="27"`) {
		t.Fatal(svg, err)
	}
	for _, bad := range []string{`fn frame(t: f64){return 2;}`, `fn frame(t: f64){loop {}}`, `fn frame(t: f64){return [anim_line(vec2(0,0),vec2(1,1),-1,2)];}`, `fn frame(t: f64){return [anim_path([vec2(0,0),vec2(1,1)],true,0,0,1)];}`} {
		out, err := Bake(bad, 24, 3)
		if err == nil || len(out.Frames) != 0 {
			t.Fatal("accepted", bad, err)
		}
	}
}

func TestBundledModuleDemo(t *testing.T) {
	source, err := os.ReadFile("../../examples/animation2d.ing")
	if err != nil {
		t.Fatal(err)
	}
	d, err := Bake(string(source), 24, 120)
	if err != nil {
		t.Fatal(err)
	}
	if len(d.Frames) != 120 || len(d.Frames[0]) != 4 || d.Frames[60][2].Kind != 4 {
		t.Fatal("incomplete demo")
	}
	if d.Frames[0][2].Points[0] == d.Frames[60][2].Points[0] {
		t.Fatal("star did not move")
	}
}
