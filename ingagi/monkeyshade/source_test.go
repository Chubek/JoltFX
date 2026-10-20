package shader

import (
	"context"
	"strings"
	"testing"
)

func TestStandaloneSource(t *testing.T) {
	source := `shader A { compute {} } shader B { fragment { gl_FragDepth = 0.5; } }`
	s, err := Parse(source, "B")
	if err != nil || s.Name != "B" {
		t.Fatalf("selection: %v", err)
	}
	data, err := CompileSource(context.Background(), source, "B", "fragment", "glsl", ToolOptions{})
	if err != nil || !strings.Contains(string(data), "gl_FragDepth") {
		t.Fatalf("compile: %s %v", data, err)
	}
	for _, test := range []struct{ source, name string }{{source, ""}, {source, "Missing"}, {"", ""}, {"fn main() {} shader S {compute{}}", "S"}, {"shader S {compute{}} shader S {compute{}}", "S"}, {strings.Repeat(" ", 1024*1024+1), ""}, {"shader {", ""}, {"import lib; shader S { compute{} }", "S"}, {"module lib; shader S {compute{}}", "S"}} {
		if _, err := Parse(test.source, test.name); err == nil {
			t.Fatalf("accepted invalid source/name %q", test.name)
		}
	}
	if _, err := CompileSource(nil, `shader S { compute {} }`, "S", "compute", "glsl", ToolOptions{}); err == nil {
		t.Fatal("nil context")
	}
}
