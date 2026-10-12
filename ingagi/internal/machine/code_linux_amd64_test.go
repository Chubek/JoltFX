//go:build linux && amd64 && cgo

package machine

import (
	"sync"
	"testing"
)

func TestExecutableLifetime(t *testing.T) {
	if _, err := Load(nil); err == nil {
		t.Fatal("accepted empty code")
	}
	// ENDBR64; mov rax,rdi; add rax,rsi; ret -- the uniform eight-i64 ABI.
	code, err := Load([]byte{0xf3, 0x0f, 0x1e, 0xfa, 0x48, 0x89, 0xf8, 0x48, 0x01, 0xf0, 0xc3})
	if err != nil {
		t.Fatal(err)
	}
	defer code.Close()
	if got, err := code.Call([8]int64{20, 22}); err != nil || got != 42 {
		t.Fatalf("%d: %v", got, err)
	}
	if err := code.Close(); err != nil {
		t.Fatal(err)
	}
	if _, err := code.Call([8]int64{}); err == nil {
		t.Fatal("called released code")
	}
	if err := code.Close(); err != nil {
		t.Fatal(err)
	}
}

func TestCallCloseConcurrent(t *testing.T) {
	code, err := Load([]byte{0xf3, 0x0f, 0x1e, 0xfa, 0xb8, 42, 0, 0, 0, 0xc3})
	if err != nil {
		t.Fatal(err)
	}
	var workers sync.WaitGroup
	start := make(chan struct{})
	for i := 0; i < 8; i++ {
		workers.Add(1)
		go func() {
			defer workers.Done()
			<-start
			for j := 0; j < 100; j++ {
				got, err := code.Call([8]int64{})
				if err == nil && got != 42 {
					t.Errorf("got %d", got)
				}
			}
		}()
	}
	close(start)
	if err := code.Close(); err != nil {
		t.Fatal(err)
	}
	workers.Wait()
}
