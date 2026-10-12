//go:build !linux || !cgo

package keystone

import "fmt"

func Assemble(library, source string) ([]byte, error) {
	return nil, fmt.Errorf("keystone: machine-code assembly currently requires Linux and CGO_ENABLED=1; QBE IL/assembly export is portable")
}
