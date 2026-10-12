//go:build linux && cgo

package keystone

import (
	"bytes"
	"os"
	"strings"
	"testing"
)

func TestMissingLibrary(t *testing.T) {
	if code, err := Assemble("/nonexistent/ingagi/libkeystone.so", "ret"); err == nil || len(code) != 0 {
		t.Fatal("accepted missing library")
	}
	if _, err := Assemble("", "ret"); err == nil {
		t.Fatal("accepted empty library path")
	}
	if _, err := Assemble("invalid\x00path", "ret"); err == nil {
		t.Fatal("accepted NUL in path")
	}
}

func TestKeystoneAssemblyAndErrors(t *testing.T) {
	lib := os.Getenv("INGAGI_KEYSTONE_LIBRARY")
	if lib == "" {
		t.Skip("set INGAGI_KEYSTONE_LIBRARY for real assembler tests")
	}
	code, err := Assemble(lib, "movl $0x2a, %eax\nret")
	if err != nil || !bytes.Equal(code, []byte{0xb8, 0x2a, 0, 0, 0, 0xc3}) {
		t.Fatalf("machine code %x: %v", code, err)
	}
	for _, source := range []string{"not_an_instruction", "call missing_symbol", "ret\x00ret"} {
		code, err := Assemble(lib, source)
		if err == nil || len(code) != 0 || !strings.Contains(err.Error(), "keystone:") {
			t.Fatalf("accepted %q: %x %v", source, code, err)
		}
	}
}
