//go:build linux && amd64 && cgo

// Package machine owns executable mappings for compiler-generated native code.
package machine

/*
#include <stdint.h>
// Compiler emits exactly this signature, including all eight arguments.
static int64_t ig_machine_call(void *entry, const int64_t *a) {
    typedef int64_t (*function)(int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t,int64_t);
    return ((function)entry)(a[0],a[1],a[2],a[3],a[4],a[5],a[6],a[7]);
}
*/
import "C"

import (
	"fmt"
	"sync"
	"syscall"
	"unsafe"
)

// Code must not be copied. Call and Close serialize access to the mapping.
type Code struct {
	mu    sync.Mutex
	bytes []byte
}

func Available() bool { return true }

func Load(data []byte) (*Code, error) {
	if len(data) == 0 || len(data) > 64<<20 {
		return nil, fmt.Errorf("jit: invalid machine-code size %d", len(data))
	}
	memory, err := syscall.Mmap(-1, 0, len(data), syscall.PROT_READ|syscall.PROT_WRITE, syscall.MAP_PRIVATE|syscall.MAP_ANON)
	if err != nil {
		return nil, fmt.Errorf("jit: map code: %w", err)
	}
	copy(memory, data)
	// No mapping is writable and executable simultaneously. x86 instruction and
	// data caches are coherent; other architectures need their own cache bridge.
	if err := syscall.Mprotect(memory, syscall.PROT_READ|syscall.PROT_EXEC); err != nil {
		syscall.Munmap(memory)
		return nil, fmt.Errorf("jit: protect code: %w", err)
	}
	return &Code{bytes: memory}, nil
}

func (c *Code) Call(args [8]int64) (int64, error) {
	if c == nil {
		return 0, fmt.Errorf("jit: missing code")
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.bytes) == 0 {
		return 0, fmt.Errorf("jit: function is closed")
	}
	return int64(C.ig_machine_call(unsafe.Pointer(&c.bytes[0]), (*C.int64_t)(unsafe.Pointer(&args[0])))), nil
}

func (c *Code) Close() error {
	if c == nil {
		return nil
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if len(c.bytes) == 0 {
		return nil
	}
	if err := syscall.Munmap(c.bytes); err != nil {
		return fmt.Errorf("jit: unmap code: %w", err)
	}
	c.bytes = nil
	return nil
}
