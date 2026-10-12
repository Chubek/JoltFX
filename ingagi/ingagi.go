// Package ingagi compiles source for embedding in Go applications. The runtime
// package provides VM instances, host functions, ECS worlds and execution hooks.
package ingagi

import (
	"context"
	"errors"

	"github.com/Chubek/JoltFX/ingagi/compiler"
	"github.com/Chubek/JoltFX/ingagi/frontend"
	"github.com/Chubek/JoltFX/ingagi/runtime"
)

// NativeOptions selects QBE IL, target assembly, an ELF object or machine code.
type NativeOptions = compiler.NativeOptions

// CompileNative compiles the typed native subset through QBE and, for machine
// code/object output, Keystone. It never executes the result. The caller's
// context bounds the QBE process; Keystone assembly is synchronous.
func CompileNative(ctx context.Context, source string, options NativeOptions) ([]byte, error) {
	parsed := frontend.Parse(source)
	var diagnostics []error
	for _, d := range parsed.Errors {
		diagnostics = append(diagnostics, d)
	}
	if len(diagnostics) != 0 {
		return nil, errors.Join(diagnostics...)
	}
	p, err := compiler.EmitQBE(parsed.File)
	if err != nil {
		return nil, err
	}
	return p.Build(ctx, options)
}

// Compile parses source, lowers it to bytecode and optimizes the resulting
// module. It does not execute initializers or perform the planned static type
// analyses. Dynamic names are resolved by the VM, allowing host-function calls.
// The novoparse build tag enables native recognition before typed-AST lowering.
func Compile(source string) (*runtime.Module, error) {
	parsed := frontend.Parse(source)
	var diagnostics []error
	for _, d := range parsed.Errors {
		diagnostics = append(diagnostics, d)
	}
	if len(diagnostics) != 0 {
		return nil, errors.Join(diagnostics...)
	}
	m, errs := runtime.Compile(parsed.File)
	for _, d := range errs {
		diagnostics = append(diagnostics, errors.New(d))
	}
	if len(diagnostics) != 0 {
		return nil, errors.Join(diagnostics...)
	}
	m.Tape = runtime.Optimize(m.Tape)
	return m, nil
}
