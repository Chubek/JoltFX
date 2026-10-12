package compiler

import (
	"bytes"
	"context"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
)

func TestWASMBuildGate(t *testing.T) {
	f := parseNative(t, `fn main() -> i64 { return 42; }`)
	for _, options := range []NativeOptions{{Emit: "wasm"}, {Target: "wasm32"}, {Target: "WASM"}} {
		data, err := BuildNative(context.Background(), f, options)
		if err != nil || len(data) < 8 || string(data[:4]) != "\x00asm" {
			t.Fatalf("%+v: %x %v", options, data, err)
		}
	}
	for _, options := range []NativeOptions{{Emit: "wasm", Target: "arm64"}, {Target: "WASM", Emit: "obj"}, {Emit: "wasm", Entry: "missing"}} {
		if _, err := BuildNative(context.Background(), f, options); err == nil {
			t.Fatalf("accepted %+v", options)
		}
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := BuildNative(ctx, f, NativeOptions{Emit: "wasm"}); err == nil {
		t.Fatal("ignored cancellation")
	}
	if _, err := BuildNative(nil, f, NativeOptions{Emit: "wasm"}); err == nil {
		t.Fatal("accepted nil context")
	}
	if _, err := EmitWASM(parseNative(t, `fn main() -> i64 { return host_read(); }`), ""); err == nil {
		t.Fatal("implicitly imported a host function")
	}
}

func TestWASMExecution(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node is required for WebAssembly execution")
	}
	for _, tc := range []struct{ name, src, expression, want string }{
		{"loops", `fn main() -> i64 { let x = 0; while x < 10 { x += 1; if x == 3 { continue; } if x == 8 { break; } } return x + 34; }`, `m.exports.ingagi_entry()`, "42"},
		{"recursion", `fn fact(n: i64) -> i64 { if n <= 1 { return 1; } return n * fact(n - 1); } fn main() -> i64 { return fact(6); }`, `m.exports.ingagi_entry()`, "720"},
		{"short-circuit", `fn trap() -> bool { return (1 / 0) == 0; } fn main() -> bool { return false && trap() || true; }`, `m.exports.ingagi_entry()`, "1"},
		{"ternary", `fn main() -> i64 { let x = 4; return x > 3 ? x * 10 + 2 : 0; }`, `m.exports.ingagi_entry()`, "42"},
		{"scopes", `fn main() -> i64 { let x = 40; { let x = 9; x += 1; } return x + 2; }`, `m.exports.ingagi_entry()`, "42"},
		{"bool-ABI", `fn main(x: bool) -> bool { return x; }`, `m.exports.ingagi_entry(256)`, "1"},
		{"signed", `fn main() -> i64 { return -23 / 5 + -23 % 5; }`, `m.exports.ingagi_entry()`, "-7"},
		{"loop", `fn main() -> i64 { let x = 0; loop { x += 1; if x == 42 { break; } } return x; }`, `m.exports.ingagi_entry()`, "42"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			f := parseNative(t, tc.src)
			a, err := EmitWASM(f, "main")
			if err != nil {
				t.Fatal(err)
			}
			b, err := EmitWASM(f, "main")
			if err != nil || !bytes.Equal(a, b) {
				t.Fatal("nondeterministic WASM", err)
			}
			file := filepath.Join(t.TempDir(), "program.wasm")
			if err := os.WriteFile(file, a, 0600); err != nil {
				t.Fatal(err)
			}
			out, err := exec.Command(node, "-e", `const fs=require('fs'); const m=new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(process.argv[1]))); console.log(String(`+tc.expression+`));`, file).CombinedOutput()
			if err != nil || string(bytes.TrimSpace(out)) != tc.want {
				t.Fatalf("execution: %s, %v; want %s", out, err, tc.want)
			}
		})
	}
}

func TestWASMTraps(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("node unavailable")
	}
	for _, expression := range []string{"1 / 0", "1 % 0", "1 << 64", "1 >> -1", "(-9223372036854775807 - 1) % -1", "(-9223372036854775807 - 1) / -1"} {
		data, err := EmitWASM(parseNative(t, `fn main() -> i64 { return `+expression+`; }`), "main")
		if err != nil {
			t.Fatal(err)
		}
		file := filepath.Join(t.TempDir(), "trap.wasm")
		if err := os.WriteFile(file, data, 0600); err != nil {
			t.Fatal(err)
		}
		out, err := exec.Command(node, "-e", `const fs=require('fs'); const m=new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(process.argv[1]))); try {m.exports.ingagi_entry(); process.exit(1);} catch(e) {if (!(e instanceof WebAssembly.RuntimeError)) throw e;}`, file).CombinedOutput()
		if err != nil {
			t.Fatalf("%s did not trap: %s %v", expression, out, err)
		}
	}
}
