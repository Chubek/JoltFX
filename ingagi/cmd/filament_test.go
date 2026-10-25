package main

import (
	"bytes"
	"os"
	"path/filepath"
	"testing"
)

func TestFilamentCLI(t *testing.T) {
	dir := t.TempDir()
	module := `module surface; public shader Surface(target:"filament", shading_model:"unlit") {
 uniform Params { color: vec4; } p;
 fragment {prepareMaterial(material);material.baseColor=p.color;}
}`
	source, output := filepath.Join(dir, "main.ing"), filepath.Join(dir, "material.mat")
	if err := os.WriteFile(filepath.Join(dir, "surface.ing"), []byte(module), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(source, []byte("import surface;"), 0600); err != nil {
		t.Fatal(err)
	}
	var out, diag bytes.Buffer
	args := []string{"shader", "-shader", "surface::Surface", "-emit", "filament", "-o", output, source}
	if code := runCLI(args, &out, &diag); code != 0 {
		t.Fatalf("%d: %s", code, &diag)
	}
	old, err := os.ReadFile(output)
	if err != nil || !bytes.Contains(old, []byte("materialParams.Params_color")) {
		t.Fatalf("%s: %v", old, err)
	}
	if code := runCLI([]string{"check", source}, &out, &diag); code != 0 {
		t.Fatalf("check: %d: %s", code, &diag)
	}
	if code := runCLI([]string{"shader", "-emit", "reflect", "-o", filepath.Join(dir, "material.json"), source}, &out, &diag); code != 0 {
		t.Fatalf("reflection: %d: %s", code, &diag)
	}
	for _, extra := range [][]string{{"-stage", "fragment"}, {"-emit", "filamat", "-matc", "/missing/matc"}} {
		fail := append(append([]string{}, args[:len(args)-1]...), extra...)
		fail = append(fail, source)
		if code := runCLI(fail, &out, &diag); code != 1 {
			t.Fatalf("%v: %d: %s", fail, code, &diag)
		}
		after, _ := os.ReadFile(output)
		if !bytes.Equal(old, after) {
			t.Fatal("failed compilation replaced existing output")
		}
	}
	if matc := os.Getenv("INGAGI_MATC"); matc != "" {
		if code := runCLI([]string{"shader", "-emit", "filamat", "-matc", matc, "-filament-api", "opengl", "-filament-platform", "mobile", "-o", filepath.Join(dir, "material.filamat"), source}, &out, &diag); code != 0 {
			t.Fatalf("package: %d: %s", code, &diag)
		}
	}
}
