//go:build linux && cgo

// Package keystone provides a private bridge to the vendored Keystone C ABI.
package keystone

/*
#cgo CFLAGS: -I${SRCDIR}/../../../third_party/keystone/include
#cgo LDFLAGS: -ldl
#include "bridge.h"
*/
import "C"

import (
	"fmt"
	"strings"
	"unsafe"
)

// Assemble uses x86-64 AT&T syntax. Library is an explicit shared-library path;
// there is no fallback to a different assembler. Returned bytes are Go-owned.
func Assemble(library, source string) ([]byte, error) {
	if strings.ContainsRune(library, 0) || strings.ContainsRune(source, 0) {
		return nil, fmt.Errorf("keystone: NUL in library path or assembly")
	}
	if library == "" {
		return nil, fmt.Errorf("keystone: set -keystone or INGAGI_KEYSTONE_LIBRARY to libkeystone.so")
	}
	path, text := append([]byte(library), 0), append([]byte(source), 0)
	var result C.ig_ks_result
	C.ig_ks_assemble((*C.char)(unsafe.Pointer(&path[0])), (*C.char)(unsafe.Pointer(&text[0])), &result)
	defer C.ig_ks_dispose(&result)
	if result.error[0] != 0 {
		return nil, fmt.Errorf("keystone: %s", C.GoString(&result.error[0]))
	}
	if result.size == 0 || uint64(result.size) > 1<<30 {
		return nil, fmt.Errorf("keystone: invalid machine-code size %d", uint64(result.size))
	}
	return C.GoBytes(unsafe.Pointer(result.bytes), C.int(result.size)), nil
}
