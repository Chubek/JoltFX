package main

import (
	"bytes"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func TestAnimationCLI(t *testing.T) {
	dir := t.TempDir()
	src, dst := filepath.Join(dir, "a.ing"), filepath.Join(dir, "a.html")
	source := `import std::animation2d as a; fn frame(t: f64) { return [a::rect(vec2(t,0),vec2(t+20,20),a::rgba(1,0,0,1),0,1)]; }`
	if err := os.WriteFile(src, []byte(source), 0600); err != nil {
		t.Fatal(err)
	}
	for _, target := range []string{"canvas", "svg", "drawing"} {
		var out, diag bytes.Buffer
		if code := runCLI([]string{"animation", "-target", target, "-frames", "3", "-o", dst, src}, &out, &diag); code != 0 {
			t.Fatal(code, &diag)
		}
		data, err := os.ReadFile(dst)
		if err != nil || len(data) == 0 {
			t.Fatal(err)
		}
	}
	if err := os.WriteFile(src, []byte(`fn frame(t: f64) {loop {}}`), 0600); err != nil {
		t.Fatal(err)
	}
	before, _ := os.ReadFile(dst)
	var out, diag bytes.Buffer
	if runCLI([]string{"animation", "-o", dst, src}, &out, &diag) == 0 || !strings.Contains(diag.String(), "budget") {
		t.Fatal(&diag)
	}
	after, _ := os.ReadFile(dst)
	if !bytes.Equal(before, after) {
		t.Fatal("failed export replaced destination")
	}
}
