//go:build !linux || !amd64 || !cgo

package machine

import "fmt"

type Code struct{}

func Available() bool { return false }
func Load([]byte) (*Code, error) {
	return nil, fmt.Errorf("jit: executable mappings require Linux amd64 with CGO")
}
func (*Code) Call([8]int64) (int64, error) {
	return 0, fmt.Errorf("jit: executable mappings require Linux amd64 with CGO")
}
func (*Code) Close() error { return nil }
