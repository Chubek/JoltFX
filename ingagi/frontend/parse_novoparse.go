//go:build novoparse && cgo

package frontend

/*
#cgo CFLAGS: -I${SRCDIR}/../../third_party/novoparse/runtime
#cgo LDFLAGS: -lnovoparse
#include "generated/ingagi.h"
*/
import "C"

import (
	"encoding/json"
	"unsafe"
)

// ConcreteResult contains the native NovoParse syntax tree, copied into Go
// storage before the native parser is destroyed. Ambiguous reports whether
// deterministic first-production selection was needed.
type ConcreteResult struct {
	Tree      json.RawMessage
	Ambiguous bool
	Errors    []Error
}

// ParseConcrete runs the generated scannerless grammar against the complete
// input. A separate native parser is owned by each call; no C pointer escapes.
func ParseConcrete(src string) ConcreteResult {
	var message [512]C.char
	p := C.Ingagi_create(&message[0], C.size_t(len(message)))
	if p == nil {
		return ConcreteResult{Errors: []Error{{Msg: "NovoParse: " + C.GoString(&message[0])}}}
	}
	defer C.np_parser_destroy(p)
	status := C.np_parser_parse(p, (*C.char)(unsafe.Pointer(unsafe.StringData(src))), C.size_t(len(src)))
	if status != C.NP_SUCCESS {
		return ConcreteResult{Errors: []Error{{Msg: "NovoParse: " + C.GoString(C.np_parser_error(p)),
			Span: Span{Start: int(C.np_parser_position(p)), End: int(C.np_parser_position(p)),
				Line: int(C.np_parser_line(p)), Col: int(C.np_parser_column(p))}}}}
	}
	tree := C.np_parser_json(p)
	if tree == nil {
		return ConcreteResult{Errors: []Error{{Msg: "NovoParse: cannot serialize syntax tree"}}}
	}
	return ConcreteResult{Tree: json.RawMessage(C.GoString(tree)), Ambiguous: C.np_parser_ambiguous(p) != 0}
}

func parseSource(src string) *Result {
	native := ParseConcrete(src)
	if len(native.Errors) != 0 {
		return &Result{File: &File{}, Errors: native.Errors}
	}
	// Transitional typed-AST lowering: native recognition is mandatory here,
	// but the typed AST still uses the existing recursive-descent implementation.
	return NewParser(src).ParseFile()
}
