package compiler

import (
	"bytes"
	"context"
	"debug/elf"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"testing"
	"time"

	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func TestELFObject(t *testing.T) {
	code := []byte{0xb8, 42, 0, 0, 0, 0xc3}
	f, err := elf.NewFile(bytes.NewReader(elfObject(code)))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	if f.Type != elf.ET_REL || f.Machine != elf.EM_X86_64 {
		t.Fatal(f.FileHeader)
	}
	text, err := f.Section(".text").Data()
	if err != nil || !bytes.Equal(text, code) {
		t.Fatalf("text %x: %v", text, err)
	}
	syms, err := f.Symbols()
	if err != nil || len(syms) != 1 || syms[0].Name != "ingagi_entry" || syms[0].Value != 0 || elf.ST_BIND(syms[0].Info) != elf.STB_GLOBAL {
		t.Fatalf("symbols %v: %v", syms, err)
	}
	if s := f.Section(".note.GNU-stack"); s == nil || s.Flags != 0 {
		t.Fatal("missing non-executable-stack annotation")
	}
}

func TestAssemblyAdapterRejectsUnsupportedData(t *testing.T) {
	for _, directive := range []string{".data", ".quad external", ".section .rodata", ".byte 42"} {
		if _, err := keystoneAssembly("ig_fn_0:\nret\n"+directive, "ig_fn_0"); err == nil {
			t.Fatalf("accepted %s", directive)
		}
	}
	if _, err := keystoneAssembly("other:\nret", "missing"); err == nil {
		t.Fatal("accepted missing entry")
	}
}

func TestNativeBuildOptions(t *testing.T) {
	p, err := EmitQBE(parseNative(t, `fn main() -> i64 { return 42; }`))
	if err != nil {
		t.Fatal(err)
	}
	for _, o := range []NativeOptions{{Emit: "wat"}, {Target: "unknown"}, {Entry: "missing"}, {Target: "rv64", Emit: "obj"}} {
		if _, err := p.Build(context.Background(), o); err == nil {
			t.Fatalf("accepted %+v", o)
		}
	}
	if _, err := p.Build(context.Background(), NativeOptions{Emit: "qbe", QBE: "/missing"}); err != nil {
		t.Fatal("IL export needs no external tools", err)
	}
}

func TestNativeBuildMalformedAndCancelled(t *testing.T) {
	p, err := EmitQBE(parseNative(t, `fn main() -> i64 { return 42; }`))
	if err != nil {
		t.Fatal(err)
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := p.Build(ctx, NativeOptions{Emit: "qbe"}); err != context.Canceled {
		t.Fatalf("cancelled build: %v", err)
	}
	if _, err := (*QBEProgram)(nil).Build(context.Background(), NativeOptions{}); err == nil {
		t.Fatal("accepted nil program")
	}
	if _, err := p.Build(nil, NativeOptions{}); err == nil {
		t.Fatal("accepted nil context")
	}
	for _, name := range []string{"", "bad\njmp other", "0bad", "bad;ret"} {
		p.Symbols["main"] = name
		if _, err := p.Build(context.Background(), NativeOptions{Emit: "qbe"}); err == nil {
			t.Fatalf("accepted symbol %q", name)
		}
	}
	p.Symbols["main"] = "ig_fn_0"
	delete(p.Signatures, "main")
	if _, err := p.Build(context.Background(), NativeOptions{Emit: "qbe"}); err == nil {
		t.Fatal("accepted missing signature")
	}
}

func TestQBETargets(t *testing.T) {
	qbe := os.Getenv("INGAGI_QBE")
	if qbe == "" {
		t.Skip("set INGAGI_QBE to the built vendored QBE")
	}
	p, err := EmitQBE(parseNative(t, `fn twice(x: i64) -> i64 { return x * 2; } fn main() -> i64 { return twice(21); }`))
	if err != nil {
		t.Fatal(err)
	}
	for _, target := range []string{"amd64_sysv", "amd64_apple", "arm64", "arm64_apple", "rv64"} {
		t.Run(target, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()
			asm, err := p.Build(ctx, NativeOptions{Emit: "asm", Target: target, QBE: qbe})
			if err != nil || len(asm) == 0 {
				t.Fatalf("assembly: %v", err)
			}
			if target == "amd64_sysv" {
				if _, err := keystoneAssembly(string(asm), p.Symbols["main"]); err != nil {
					t.Fatalf("%v\n%s", err, asm)
				}
			}
		})
	}
}

func TestQBEKeystoneExecution(t *testing.T) {
	qbe, library := os.Getenv("INGAGI_QBE"), os.Getenv("INGAGI_KEYSTONE_LIBRARY")
	if qbe == "" || library == "" {
		t.Skip("set INGAGI_QBE and INGAGI_KEYSTONE_LIBRARY for native execution tests")
	}
	if runtime.GOOS != "linux" || runtime.GOARCH != "amd64" {
		t.Skip("native execution tests require Linux amd64")
	}
	cc, err := exec.LookPath("cc")
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		name, src, entry string
		args             []int64
		want             int64
	}{
		{"arithmetic", `fn main() -> i64 { return 6 * 7; }`, "main", nil, 42},
		{"recursion", `fn fib(n: i64) -> i64 { if n < 2 { return n; } return fib(n - 1) + fib(n - 2); }`, "fib", []int64{15}, 610},
		{"loops and scope", `fn main() -> i64 {
            let total = 0; let n = 0;
            while n < 10 { n += 1; if n == 3 { continue; } if n == 8 { break; } total += n; }
            { let total = 100; total += 1; }
            return total;
        }`, "main", nil, 25},
		{"short circuit", `fn forever() -> bool { loop {} return true; }
            fn main() -> i64 { if false && forever() { return 0; } if true || forever() { return 42; } return 0; }`, "main", nil, 42},
		{"signed division", `fn main(a: i64, b: i64) -> i64 { return a / b * 100 + a % b; }`, "main", []int64{-17, 5}, -302},
		{"large truthiness", `fn main(x: i64) -> i64 { if x { return 42; } return 0; }`, "main", []int64{1 << 40}, 42},
		{"stack arguments", `fn main(a: i64, b: i64, c: i64, d: i64, e: i64, f: i64, g: i64, h: i64) -> i64 { return a + b + c + d + e + f + g + h; }`, "main", []int64{1, 2, 3, 4, 5, 6, 7, 14}, 42},
		{"boolean ABI", `fn main(x: i64) -> bool { return x < 0; }`, "main", []int64{-1}, 1},
		{"nested arguments", `fn sum(a: i64, b: i64) -> i64 { return a + b; }
            fn main() -> i64 { let x = 1; return sum(x = x + 1, x = x + 1) * 10 + x; }`, "main", nil, 53},
		{"ternary branches", `fn main(x: i64) -> i64 { let n = 0; let v = x > 0 ? (n = 42) : (n = 99); return v + n; }`, "main", []int64{1}, 84},
		{"nested ternary", `fn main(x: i64) -> i64 { return x == 1 ? 7 : x == 2 ? 42 : 0; }`, "main", []int64{2}, 42},
		{"bit operations", `fn main(x: i64) -> i64 { return ((x >> 2) & 255) ^ (3 << 4); }`, "main", []int64{-8}, 206},
		{"large negative immediate", `fn main() -> i64 { return -4294967296 + 42; }`, "main", nil, -4294967254},
	} {
		t.Run(tc.name, func(t *testing.T) {
			file := parseNative(t, tc.src)
			p, err := EmitQBE(file)
			if err != nil {
				t.Fatal(err)
			}
			ctx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
			defer cancel()
			obj, err := p.Build(ctx, NativeOptions{QBE: qbe, KeystoneLibrary: library, Entry: tc.entry})
			if err != nil {
				t.Fatalf("%v\n%s", err, p.IL)
			}
			dir := t.TempDir()
			object, driver, executable := filepath.Join(dir, "program.o"), filepath.Join(dir, "driver.c"), filepath.Join(dir, "program")
			if err := os.WriteFile(object, obj, 0600); err != nil {
				t.Fatal(err)
			}
			var params, values []string
			var vmArgs []rt.Value
			for _, n := range tc.args {
				params = append(params, "long long")
				values = append(values, strconv.FormatInt(n, 10)+"LL")
				vmArgs = append(vmArgs, rt.IntValue(n))
			}
			paramText := strings.Join(params, ",")
			if paramText == "" {
				paramText = "void"
			}
			ret := "long long"
			if p.Signatures[tc.entry].Result == Bool {
				ret = "int"
			}
			source := fmt.Sprintf("#include <stdio.h>\nextern %s ingagi_entry(%s);\nint main(void) { printf(\"%%lld\\n\", (long long)ingagi_entry(%s)); return 0; }\n", ret, paramText, strings.Join(values, ","))
			if err := os.WriteFile(driver, []byte(source), 0600); err != nil {
				t.Fatal(err)
			}
			// cc links Keystone's object with a C ABI driver. It never assembles QBE output.
			if out, err := exec.CommandContext(ctx, cc, "-o", executable, driver, object).CombinedOutput(); err != nil {
				t.Fatalf("link: %v\n%s", err, out)
			}
			out, err := exec.CommandContext(ctx, executable).CombinedOutput()
			if err != nil || strings.TrimSpace(string(out)) != strconv.FormatInt(tc.want, 10) {
				t.Fatalf("native output %q, want %d: %v", out, tc.want, err)
			}
			m, errs := rt.Compile(file)
			if len(errs) != 0 {
				t.Fatal(errs)
			}
			vm := rt.New()
			if err := vm.LoadModule(m); err != nil {
				t.Fatal(err)
			}
			v, err := vm.CallBudget(rt.Budget{MaxInstrs: 200000}, tc.entry, vmArgs...)
			if err != nil {
				t.Fatal(err)
			}
			want := v.I
			if v.K == rt.KBool && v.Truthy() {
				want = 1
			}
			if want != tc.want {
				t.Fatalf("VM/native mismatch: VM=%v native=%d", v, tc.want)
			}
		})
	}
}

func TestQBENativeTraps(t *testing.T) {
	qbe, library := os.Getenv("INGAGI_QBE"), os.Getenv("INGAGI_KEYSTONE_LIBRARY")
	if qbe == "" || library == "" {
		t.Skip("set INGAGI_QBE and INGAGI_KEYSTONE_LIBRARY for native trap tests")
	}
	if runtime.GOOS != "linux" || runtime.GOARCH != "amd64" {
		t.Skip("native execution tests require Linux amd64")
	}
	for _, tc := range []struct{ op, a, b string }{
		{"/", "42", "0"},
		{"/", "(-9223372036854775807LL-1)", "-1"},
		{"%", "(-9223372036854775807LL-1)", "-1"},
		{"<<", "1", "64"},
		{">>", "1", "-1"},
	} {
		t.Run(tc.op+tc.a+tc.b, func(t *testing.T) {
			p, err := EmitQBE(parseNative(t, "fn main(a: i64, b: i64) -> i64 { return a "+tc.op+" b; }"))
			if err != nil {
				t.Fatal(err)
			}
			ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
			defer cancel()
			obj, err := p.Build(ctx, NativeOptions{QBE: qbe, KeystoneLibrary: library})
			if err != nil {
				t.Fatal(err)
			}
			dir := t.TempDir()
			object, driver, executable := filepath.Join(dir, "trap.o"), filepath.Join(dir, "trap.c"), filepath.Join(dir, "trap")
			if err := os.WriteFile(object, obj, 0600); err != nil {
				t.Fatal(err)
			}
			// Catch the deliberate invalid-instruction trap without writing a core dump.
			src := fmt.Sprintf("#include <signal.h>\n#include <stdlib.h>\nextern long long ingagi_entry(long long,long long);\nstatic void trapped(int s) { _Exit(s == SIGILL ? 73 : 74); }\nint main(void) { signal(SIGILL, trapped); (void)ingagi_entry(%s,%s); return 0; }\n", tc.a, tc.b)
			if err := os.WriteFile(driver, []byte(src), 0600); err != nil {
				t.Fatal(err)
			}
			if out, err := exec.CommandContext(ctx, "cc", "-o", executable, driver, object).CombinedOutput(); err != nil {
				t.Fatalf("%v\n%s", err, out)
			}
			err = exec.CommandContext(ctx, executable).Run()
			if exit, ok := err.(*exec.ExitError); !ok || exit.ExitCode() != 73 {
				t.Fatalf("expected native arithmetic trap, got %v", err)
			}
		})
	}
}
