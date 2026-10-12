// Package ingagi compiles source for embedding in Go applications. The runtime
// package provides VM instances, host functions, ECS worlds and execution hooks.
package ingagi

import (
	"context"
	"errors"
	"fmt"
	"io/fs"

	"github.com/Chubek/JoltFX/ingagi/compiler"
	"github.com/Chubek/JoltFX/ingagi/frontend"
	"github.com/Chubek/JoltFX/ingagi/module_system"
	"github.com/Chubek/JoltFX/ingagi/runtime"
)

// NativeOptions selects QBE IL, target assembly, an ELF object, machine code or WASM.
type NativeOptions = compiler.NativeOptions

type NativeFunction = compiler.NativeFunction

// CompileJIT compiles and loads a scalar machine-code entry for explicit calls.
// The returned handle must be closed. Native calls have no VM execution budget.
func CompileJIT(ctx context.Context, source string, options NativeOptions) (*NativeFunction, error) {
	parsed := frontend.Parse(source)
	var diagnostics []error
	for _, d := range parsed.Errors {
		diagnostics = append(diagnostics, d)
	}
	if len(diagnostics) != 0 {
		return nil, errors.Join(diagnostics...)
	}
	return compiler.BuildJIT(ctx, parsed.File, options)
}

// CompileJITFile resolves imports before installing the native entry.
func CompileJITFile(ctx context.Context, sources fs.FS, filename string, options NativeOptions) (*NativeFunction, error) {
	f, err := (&module_system.Loader{FS: sources}).Load(filename)
	if err != nil {
		return nil, err
	}
	return compiler.BuildJIT(ctx, f, options)
}

// CompileNative compiles the typed native subset through QBE/Keystone or emits
// a standalone WASM binary. It never executes the result. The caller's context
// bounds the QBE process; Keystone assembly is synchronous.
func CompileNative(ctx context.Context, source string, options NativeOptions) ([]byte, error) {
	parsed := frontend.Parse(source)
	var diagnostics []error
	for _, d := range parsed.Errors {
		diagnostics = append(diagnostics, d)
	}
	if len(diagnostics) != 0 {
		return nil, errors.Join(diagnostics...)
	}
	return compiler.BuildNative(ctx, parsed.File, options)
}

// CompileNativeFile resolves the source graph before lowering the scalar native
// subset. Each imported declaration must itself be supported by the backend.
func CompileNativeFile(ctx context.Context, sources fs.FS, filename string, options NativeOptions) ([]byte, error) {
	f, err := (&module_system.Loader{FS: sources}).Load(filename)
	if err != nil {
		return nil, err
	}
	return compiler.BuildNative(ctx, f, options)
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
	if len(parsed.File.Imports) > 0 {
		return nil, fmt.Errorf("ingagi: imports require CompileFile with a module filesystem")
	}
	return compileFile(parsed.File)
}

// CompileFile resolves imports below sources and compiles the dependency graph.
// filename is an fs.ValidPath, relative to sources. Imported modules initialize
// once in dependency order and expose their public declarations by namespace.
func CompileFile(sources fs.FS, filename string) (*runtime.Module, error) {
	f, err := (&module_system.Loader{FS: sources}).Load(filename)
	if err != nil {
		return nil, err
	}
	return compileFile(f)
}

func compileFile(f *frontend.File) (*runtime.Module, error) {
	var diagnostics []error
	m, errs := runtime.Compile(f)
	for _, d := range errs {
		diagnostics = append(diagnostics, errors.New(d))
	}
	if len(diagnostics) != 0 {
		return nil, errors.Join(diagnostics...)
	}
	m.Tape = runtime.Optimize(m.Tape)
	return m, nil
}
