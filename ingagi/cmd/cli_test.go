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
