//go:build cgo

package main

/*
#define INGAGI_GO_EXPORTS
#include "bridge.h"
*/
import "C"

import (
	"fmt"
	"strings"
	"sync"
	"unicode/utf8"
	"unsafe"

	api "github.com/Chubek/JoltFX/ingagi/native_extension_api"
)

var handles = struct {
	sync.Mutex
	next     uint64
	sessions map[uint64]*api.Session
}{sessions: map[uint64]*api.Session{}}

func session(h C.ingagi_handle) *api.Session {
	handles.Lock()
	defer handles.Unlock()
	return handles.sessions[uint64(h)]
}
func message(dst *C.char, capacity C.size_t, text string) {
	if dst == nil || capacity == 0 {
		return
	}
	// Diagnostics need at most 4096 bytes; never create an unbounded unsafe slice.
	n := uint64(capacity)
	if n > 4096 {
		n = 4096
	}
	out := unsafe.Slice((*byte)(unsafe.Pointer(dst)), int(n))
	text = strings.ToValidUTF8(text, "\uFFFD")
	if len(text) >= len(out) {
		text = text[:len(out)-1]
		for len(text) > 0 && !utf8.ValidString(text) {
			text = text[:len(text)-1]
		}
	}
	written := copy(out[:len(out)-1], text)
	out[written] = 0
}
func finish(code api.Status, err error, dst *C.char, capacity C.size_t) C.ingagi_status {
	text := ""
	if err != nil {
		text = err.Error()
	}
	message(dst, capacity, text)
	return C.ingagi_status(code)
}
func recovered(status *C.ingagi_status, dst *C.char, capacity C.size_t) {
	if p := recover(); p != nil {
		*status = finish(api.InternalError, fmt.Errorf("Ingagi internal error: %v", p), dst, capacity)
	}
}
func text(ptr *C.char, n C.size_t, max uint64) (string, error) {
	if uint64(n) > max || ptr == nil && n != 0 {
		return "", fmt.Errorf("invalid UTF-8 buffer or length")
	}
	if n == 0 {
		return "", nil
	}
	s := string(unsafe.Slice((*byte)(unsafe.Pointer(ptr)), int(n)))
	if !utf8.ValidString(s) || strings.IndexByte(s, 0) >= 0 {
		return "", fmt.Errorf("text must be UTF-8 without NUL")
	}
	return s, nil
}
func fromC(v C.ingagi_value) (api.Value, error) {
	if v.reserved != 0 {
		return api.Value{}, fmt.Errorf("value reserved field must be zero")
	}
	return api.Value{Kind: api.Kind(v.kind), Integer: int64(v.integer), Real: float64(v.real)}, nil
}
func toC(v api.Value) C.ingagi_value {
	return C.ingagi_value{kind: C.uint32_t(v.Kind), integer: C.int64_t(v.Integer), real: C.double(v.Real)}
}

//export ingagi_abi_version
func ingagi_abi_version() C.uint32_t { return C.uint32_t(api.ABIVersion) }

//export ingagi_create
func ingagi_create(budget C.uint64_t, out *C.ingagi_handle, diag *C.char, cap C.size_t) (status C.ingagi_status) {
	defer recovered(&status, diag, cap)
	if out == nil {
		return finish(api.InvalidArgument, fmt.Errorf("missing handle output"), diag, cap)
	}
	*out = 0
	s := api.New(uint64(budget))
	handles.Lock()
	if handles.next == ^uint64(0) {
		handles.Unlock()
		return finish(api.InternalError, fmt.Errorf("handle space exhausted"), diag, cap)
	}
	handles.next++
	h := handles.next
	handles.sessions[h] = s
	handles.Unlock()
	*out = C.ingagi_handle(h)
	return finish(api.OK, nil, diag, cap)
}

//export ingagi_destroy
func ingagi_destroy(h C.ingagi_handle, diag *C.char, cap C.size_t) (status C.ingagi_status) {
	defer recovered(&status, diag, cap)
	s := session(h)
	err := s.Close()
	if err == nil {
		handles.Lock()
		delete(handles.sessions, uint64(h))
		handles.Unlock()
	}
	return finish(api.ErrorCode(err), err, diag, cap)
}

//export ingagi_load
func ingagi_load(h C.ingagi_handle, source *C.char, size C.size_t, diag *C.char, cap C.size_t) (status C.ingagi_status) {
	defer recovered(&status, diag, cap)
	src, err := text(source, size, api.MaxSource)
	if err != nil {
		return finish(api.InvalidArgument, err, diag, cap)
	}
	err = session(h).Load(src)
	return finish(api.ErrorCode(err), err, diag, cap)
}

//export ingagi_call
func ingagi_call(h C.ingagi_handle, name *C.char, size C.size_t, args *C.ingagi_value, argc C.uint32_t, out *C.ingagi_value, diag *C.char, cap C.size_t) (status C.ingagi_status) {
	defer recovered(&status, diag, cap)
	if out == nil || argc > api.MaxArgs || args == nil && argc != 0 {
		return finish(api.InvalidArgument, fmt.Errorf("invalid result/argument buffer"), diag, cap)
	}
	entry, err := text(name, size, 255)
	if err != nil {
		return finish(api.InvalidArgument, err, diag, cap)
	}
	values := make([]api.Value, int(argc))
	for i, a := range unsafe.Slice(args, int(argc)) {
		values[i], err = fromC(a)
		if err != nil {
			return finish(api.InvalidArgument, err, diag, cap)
		}
	}
	*out = C.ingagi_value{}
	v, err := session(h).Call(entry, values)
	if err == nil {
		*out = toC(v)
	}
	return finish(api.ErrorCode(err), err, diag, cap)
}

//export ingagi_register_module
func ingagi_register_module(h C.ingagi_handle, module *C.ingagi_module, diag *C.char, cap C.size_t) (status C.ingagi_status) {
	defer recovered(&status, diag, cap)
	if module == nil || module.abi_version != C.uint32_t(api.ABIVersion) || module.function_count == 0 || module.function_count > api.MaxArgs || module.functions == nil {
		return finish(api.InvalidArgument, fmt.Errorf("invalid module descriptor or ABI version"), diag, cap)
	}
	functions := make([]api.Function, 0, int(module.function_count))
	for _, descriptor := range unsafe.Slice(module.functions, int(module.function_count)) {
		name, err := text((*C.char)(unsafe.Pointer(descriptor.name)), descriptor.name_size, 255)
		if err != nil || descriptor.callback == nil || descriptor.reserved != 0 {
			return finish(api.InvalidArgument, fmt.Errorf("invalid native function descriptor"), diag, cap)
		}
		fn, user := descriptor.callback, descriptor.userdata
		functions = append(functions, api.Function{Name: name, Arity: uint32(descriptor.arity), Call: func(args []api.Value) (api.Value, error) {
			converted := make([]C.ingagi_value, len(args))
			for i, a := range args {
				converted[i] = toC(a)
			}
			var ptr *C.ingagi_value
			if len(converted) > 0 {
				ptr = &converted[0]
			}
			var result C.ingagi_value
			var errorBuffer [4096]C.char
			code := C.ig_invoke(fn, user, ptr, C.uint32_t(len(args)), &result, &errorBuffer[0], C.size_t(len(errorBuffer)))
			errorBuffer[len(errorBuffer)-1] = 0
			if code != 0 {
				return api.Value{}, fmt.Errorf("native callback status %d: %s", code, C.GoString(&errorBuffer[0]))
			}
			return fromC(result)
		}})
	}
	err := session(h).Register(functions)
	return finish(api.ErrorCode(err), err, diag, cap)
}
