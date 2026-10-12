package frontend

import (
	"context"
	"os"
	"os/exec"
	"strings"
	"testing"
	"time"
)

// Run stalled-recovery regressions in bounded child processes: a regression
// must fail this test rather than exhaust memory in the embedding host.
func TestDeclarationRecoveryTerminates(t *testing.T) {
	if source := os.Getenv("INGAGI_RECOVERY_SOURCE"); source != "" {
		if r := NewParser(source).ParseFile(); r.OK() {
			t.Fatal("accepted malformed declaration")
		}
		return
	}
	for _, source := range []string{
		`entity E { entity }`,
		`system S { entity }`,
		`system S { public }`,
		`pipeline P { entity }`,
		`fn main() { let entity = 1; print(entity); }`,
		`fn main() { let entity = spawn(); add_component(entity, "SceneObject", SceneObject { object: 0 }); let link = get_component(entity, "SceneObject"); }`,
	} {
		t.Run(source, func(t *testing.T) {
			ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
			defer cancel()
			cmd := exec.CommandContext(ctx, os.Args[0], "-test.run=^TestDeclarationRecoveryTerminates$")
			cmd.Env = append(os.Environ(), "INGAGI_RECOVERY_SOURCE="+source, "GOMEMLIMIT=64MiB")
			if output, err := cmd.CombinedOutput(); err != nil {
				t.Fatalf("recovery failed: %v: %s", err, output)
			}
		})
	}
}

// Explicit statement terminators follow the language grammar on both parsers.
func TestStatementTerminators(t *testing.T) {
	for _, src := range []string{
		`fn main() { return 1; }`,
		`fn main() { return f(); }`,
		`fn main() { let c = |x: i32| { return helper(x); }; return c(1); }`,
		`fn main() { loop { break; } }`,
		`fn main() { loop { continue; } }`,
		`fn main() { while true { break; } }`,
		`fn main() { if true { return 1; } return 2; }`,
		`fn main() { let x = 1; }`,
		`fn main() { print(1); }`,
	} {
		r := Parse(src)
		if !r.OK() {
			t.Errorf("%q: %v", src, r.Errors)
		}
	}
}

// TestStatementSemicolonStillRequired confirms the leniency did not loosen
// statement termination everywhere: a missing `;` between two statements is
// still an error.
func TestStatementSemicolonStillRequired(t *testing.T) {
	for _, src := range []string{
		`fn main() { let a = 1 let b = 2; return a; }`,
		`fn main() { return 1 return 2; }`,
		`fn main() { print(1) print(2); }`,
		`fn main() { return 1 }`,
	} {
		r := Parse(src)
		if r.OK() {
			t.Errorf("%q: accepted a missing statement terminator", src)
		}
	}
}

// TestParserTerminatesOnTruncatedInput checks the recursive-descent AST builder
// directly, including under the native-recognition build tag. Every prefix must
// return; a hang would mean a parse loop failed to advance.
func TestParserTerminatesOnTruncatedInput(t *testing.T) {
	full := `
struct P { x: i32; y: i32; }
enum S { A, B }
fn helper(a: i32) -> i32 { return a + 1; }
fn main() {
    let p = P { x: 1, y: 2 };
    let arr = [1, 2, 3];
    let f = |x: i32| { return helper(x); };
    for p in arr { p; }
    while false { continue; }
    loop { break; }
    match arr[0] { 1 => {}, _ => {} }
    return f(arr[0]);
}
system Render { update(dt: f32) { main(); } }
`
	for n := 0; n <= len(full); n++ {
		func() {
			defer func() {
				if p := recover(); p != nil {
					t.Fatalf("prefix of %d bytes panicked: %v", n, p)
				}
			}()
			NewParser(full[:n]).ParseFile()
		}()
	}
}

// TestParserTerminatesOnJunk feeds random punctuation soup that exercises the
// speculative lookahead paths.
func TestParserTerminatesOnJunk(t *testing.T) {
	junk := []string{
		strings.Repeat("|", 200),
		strings.Repeat("?", 200),
		strings.Repeat("::", 200),
		strings.Repeat("(", 200),
		strings.Repeat("{", 200),
		strings.Repeat("}", 200),
		strings.Repeat(";", 200),
		strings.Repeat("fn ", 100),
		strings.Repeat("f ? ", 60) + "1" + strings.Repeat(" : 0", 60),
		strings.Repeat("a.b.c.", 100),
		strings.Repeat("[1,", 100),
	}
	for _, src := range junk {
		NewParser(src).ParseFile()
	}
}

// FuzzParse asserts Parse never panics and always terminates for arbitrary
// input. Only crashes count as failures; rejected syntax is normal.
func FuzzParse(f *testing.F) {
	f.Add(`fn main() { return 1; }`)
	f.Add(`fn main() { let f = |x: i32| { return x?; }; return f(1); }`)
	f.Add(``)
	f.Add(`struct P { x: i32; }`)
	f.Add(`entity E { entity }`)
	f.Add(`system S { public }`)
	f.Add(`pipeline P { entity }`)
	f.Fuzz(func(t *testing.T, src string) {
		r := Parse(src)
		// A successful parse must yield a file; a failed one must carry at
		// least one diagnostic.
		if r.OK() && r.File == nil {
			t.Fatal("Parse reported success with a nil file")
		}
		if !r.OK() && len(r.Errors) == 0 {
			t.Fatal("Parse reported failure with no diagnostics")
		}
	})
}

// FuzzParseExpression applies the same invariants to the standalone expression
// entry point used by hosts and tests.
func FuzzParseExpression(f *testing.F) {
	f.Add(`1 + 2`)
	f.Add(`flag ? 1 : 2`)
	f.Add(`f()?`)
	f.Add(``)
	f.Fuzz(func(t *testing.T, src string) {
		e, errs := ParseExpression(src)
		if len(errs) == 0 && e == nil {
			t.Fatal("ParseExpression reported success with a nil expression")
		}
	})
}
