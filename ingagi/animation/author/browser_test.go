package author

import (
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestCanvasBrowser(t *testing.T) {
	if os.Getenv("INGAGI_CANVAS_BROWSER_TEST") != "1" {
		t.Skip("set INGAGI_CANVAS_BROWSER_TEST=1 for real Firefox Canvas tests")
	}
	d, err := Bake(`import std::animation2d as a; fn frame(t: f64) {let x=20+t*480;return [a::rect(vec2(x,20),vec2(x+10,30),a::rgba(1,0,0,0.5),0,1)];}`, 24, 2)
	if err != nil {
		t.Fatal(err)
	}
	html, err := d.HTML()
	if err != nil {
		t.Fatal(err)
	}
	path := filepath.Join(t.TempDir(), "animation.html")
	if err = os.WriteFile(path, []byte(html), 0600); err != nil {
		t.Fatal(err)
	}
	if out, err := exec.Command("node", "../scripts/test-canvas.mjs", path).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
}
