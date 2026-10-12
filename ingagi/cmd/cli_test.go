package main

import (
	"bytes"
	"os"
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
