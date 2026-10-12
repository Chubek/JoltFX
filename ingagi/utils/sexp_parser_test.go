package utils

import (
	"strings"
	"testing"
)

func TestSexpMalformedAndEmptyList(t *testing.T) {
	for _, source := range []string{")", "(x))", "(x", strings.Repeat("(", 129), strings.Repeat("'", 129) + "x"} {
		if _, err := Parse(source); err == nil {
			t.Fatalf("accepted %q", source)
		}
	}
	n, err := ParseOne("()")
	if err != nil || !n.IsList() || n.IsAtom() || n.String() != "()" {
		t.Fatalf("empty list: %v %v", n, err)
	}
	n, err = ParseOne(`(target (description "quoted value") (items a b))`)
	if err != nil || len(n.Field("target")) != 2 {
		t.Fatalf("record: %v %v", n, err)
	}
}
