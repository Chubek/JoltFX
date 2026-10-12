package main

import (
	"bytes"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestCLI(t *testing.T) {
	for _, tc := range []struct {
		name, source       string
		args               []string
		code               int
		output, diagnostic string
	}{
		{"run", `fn main() { print("hello"); return 42; }`, []string{"run"}, 0, "hello\n42\n", ""},
		{"check does not execute", `fn main() { print("unexpected"); }`, []string{"check"}, 0, "", ""},
		{"check types", `fn main() -> i64 { return "wrong"; }`, []string{"check"}, 1, "", "E0201"},
		{"check names", `fn main() { return missing; }`, []string{"check"}, 1, "", "E0102"},
		{"dump", `fn main() { return 42; }`, []string{"dump"}, 0, "push_int 42", ""},
		{"parse error", `fn main( {`, []string{"run"}, 1, "", "sample.ing: 1:"},
		{"compile error", `fn main() { break; }`, []string{"run"}, 1, "", "break outside loop"},
		{"runtime error", `fn main() { return missing; }`, []string{"run"}, 1, "", "undefined name"},
		{"instruction budget", `fn main() { loop {} }`, []string{"run", "-budget", "30"}, 1, "", "budget exceeded"},
		{"entry", `fn other() { return 7; }`, []string{"run", "-entry", "other"}, 0, "7\n", ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			path := filepath.Join(t.TempDir(), "sample.ing")
			if err := os.WriteFile(path, []byte(tc.source), 0600); err != nil {
				t.Fatal(err)
			}
			var out, diag bytes.Buffer
			code := runCLI(append(tc.args, path), &out, &diag)
			if code != tc.code || !strings.Contains(out.String(), tc.output) || !strings.Contains(diag.String(), tc.diagnostic) {
				t.Fatalf("exit=%d output=%q diagnostic=%q", code, out.String(), diag.String())
			}
			if tc.output == "" && out.Len() != 0 {
				t.Fatalf("unexpected output %q", out.String())
			}
			if tc.diagnostic == "" && diag.Len() != 0 {
				t.Fatalf("unexpected diagnostic %q", diag.String())
			}
		})
	}
}

func TestCLIImports(t *testing.T) {
	dir := t.TempDir()
	main, dep := filepath.Join(dir, "main.ing"), filepath.Join(dir, "dep.ing")
	if err := os.WriteFile(main, []byte(`import dep; fn main() -> i64 { return dep::value(); }`), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(dep, []byte(`public fn value() -> i64 { return 42; }`), 0600); err != nil {
		t.Fatal(err)
	}
	for _, cmd := range []string{"check", "run"} {
		var out, diag bytes.Buffer
		if code := runCLI([]string{cmd, main}, &out, &diag); code != 0 || diag.Len() != 0 {
			t.Fatalf("%s: %d, %s", cmd, code, &diag)
		}
		if cmd == "run" && out.String() != "42\n" {
			t.Fatal(out.String())
		}
	}
}

func TestShaderCLI(t *testing.T) {
	dir := t.TempDir()
	src, dst := filepath.Join(dir, "shader.ing"), filepath.Join(dir, "shader.comp")
	if err := os.WriteFile(src, []byte(`shader S { compute(local_size: 1) {} }`), 0600); err != nil {
		t.Fatal(err)
	}
	var out, diag bytes.Buffer
	args := []string{"shader", "-emit", "glsl", "-o", dst, src}
	if code := runCLI(args, &out, &diag); code != 0 {
		t.Fatalf("%d: %s", code, &diag)
	}
	data, err := os.ReadFile(dst)
	if err != nil || !bytes.Contains(data, []byte("void main()")) {
		t.Fatalf("%s: %v", data, err)
	}
	if err := os.WriteFile(src, []byte(`shader S { compute { defer {} } }`), 0600); err != nil {
		t.Fatal(err)
	}
	if code := runCLI(args, &out, &diag); code != 1 {
		t.Fatalf("%d: %s", code, &diag)
	}
	after, err := os.ReadFile(dst)
	if err != nil || !bytes.Equal(data, after) {
		t.Fatal("failed shader build replaced artifact")
	}
}

func TestNativeCLIExport(t *testing.T) {
	dir := t.TempDir()
	src, dst := filepath.Join(dir, "source.ing"), filepath.Join(dir, "source.ssa")
	if err := os.WriteFile(src, []byte(`fn main() -> i64 { return 42; }`), 0600); err != nil {
		t.Fatal(err)
	}
	var out, diag bytes.Buffer
	code := runCLI([]string{"build", "-emit", "qbe", "-o", dst, src}, &out, &diag)
	if code != 0 || out.Len() != 0 || diag.Len() != 0 {
		t.Fatalf("exit=%d output=%s diagnostic=%s", code, &out, &diag)
	}
	old, err := os.ReadFile(dst)
	if err != nil || !bytes.Contains(old, []byte("$ingagi_entry")) {
		t.Fatalf("artifact %s: %v", old, err)
	}
	if err := os.WriteFile(src, []byte(`fn main() -> i64 { return missing; }`), 0600); err != nil {
		t.Fatal(err)
	}
	if runCLI([]string{"build", "-emit", "qbe", "-o", dst, src}, &out, &diag) != 1 {
		t.Fatal("expected native diagnostic")
	}
	got, err := os.ReadFile(dst)
	if err != nil || !bytes.Equal(got, old) {
		t.Fatal("failed compilation replaced previous artifact")
	}
	if runCLI([]string{"build", "-emit", "qbe", "-o", src, src}, &out, &diag) != 2 {
		t.Fatal("accepted source as output")
	}
}

func TestTargetDiscovery(t *testing.T) {
	var out, diag bytes.Buffer
	if code := runCLI([]string{"targets"}, &out, &diag); code != 0 {
		t.Fatalf("%d: %s", code, &diag)
	}
	if !bytes.Contains(out.Bytes(), []byte("QBE\timplemented")) || !bytes.Contains(out.Bytes(), []byte("WebGPU\tplanned")) {
		t.Fatalf("%s", &out)
	}
	if code := runCLI([]string{"targets", "extra"}, &out, &diag); code != 2 {
		t.Fatalf("unexpected status %d", code)
	}
}

func TestWASMCLIImports(t *testing.T) {
	dir := t.TempDir()
	src, dep, dst := filepath.Join(dir, "main.ing"), filepath.Join(dir, "dep.ing"), filepath.Join(dir, "main.wasm")
	if err := os.WriteFile(src, []byte(`import dep; fn main() -> i64 { return dep::value(); }`), 0600); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(dep, []byte(`public fn value() -> i64 { return 42; }`), 0600); err != nil {
		t.Fatal(err)
	}
	var out, diag bytes.Buffer
	args := []string{"build", "-emit", "wasm", "-o", dst, src}
	for _, selection := range [][]string{{"-emit", "wasm"}, {"-target", "wasm32"}, {"-target", "WASM"}} {
		if code := runCLI(append(append([]string{"build"}, selection...), "-o", dst, src), &out, &diag); code != 0 || diag.Len() != 0 {
			t.Fatalf("%v: %d: %s", selection, code, &diag)
		}
	}
	data, err := os.ReadFile(dst)
	if err != nil || len(data) < 8 || string(data[:4]) != "\x00asm" {
		t.Fatalf("%x: %v", data, err)
	}
	if node, err := exec.LookPath("node"); err == nil {
		result, err := exec.Command(node, "-e", `const fs=require('fs'); const m=new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(process.argv[1]))); if(m.exports.ingagi_entry()!==42n)process.exit(1);`, dst).CombinedOutput()
		if err != nil {
			t.Fatalf("%s: %v", result, err)
		}
	}
	if err := os.WriteFile(dep, []byte(`public fn value() -> i64 { return missing; }`), 0600); err != nil {
		t.Fatal(err)
	}
	if code := runCLI(args, &out, &diag); code != 1 {
		t.Fatalf("%d: %s", code, &diag)
	}
	after, err := os.ReadFile(dst)
	if err != nil || !bytes.Equal(data, after) {
		t.Fatal("failed WASM build replaced artifact")
	}
}

func TestCrossShaderCLI(t *testing.T) {
	cross := os.Getenv("INGAGI_SPIRV_CROSS")
	if cross == "" {
		t.Skip("set INGAGI_SPIRV_CROSS for cross-shader CLI execution")
	}
	dir := t.TempDir()
	src, dst := filepath.Join(dir, "shader.ing"), filepath.Join(dir, "shader.hlsl")
	if err := os.WriteFile(src, []byte(`shader S { compute(local_size: 1) {} }`), 0600); err != nil {
		t.Fatal(err)
	}
	var out, diag bytes.Buffer
	args := []string{"shader", "-emit", "hlsl", "-spirv-cross", cross, "-o", dst, src}
	if code := runCLI(args, &out, &diag); code != 0 {
		t.Fatalf("%d: %s", code, &diag)
	}
	data, err := os.ReadFile(dst)
	if err != nil || !bytes.Contains(data, []byte("numthreads")) {
		t.Fatalf("%s: %v", data, err)
	}
	args[4] = filepath.Join(dir, "missing-cross")
	if code := runCLI(args, &out, &diag); code != 1 {
		t.Fatalf("%d: %s", code, &diag)
	}
	after, err := os.ReadFile(dst)
	if err != nil || !bytes.Equal(data, after) {
		t.Fatal("failed cross-shader build replaced artifact")
	}
}
