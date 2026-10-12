package compiler

import (
	"context"
	"fmt"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"github.com/Chubek/JoltFX/ingagi/internal/machine"
)

// NativeFunction owns an explicitly compiled scalar entry. Call uses signed
// integers; bool parameters normalize any nonzero input and bool results are 0/1.
// Close releases executable memory and is safe concurrently with Call. Native
// execution has no VM budgets, cancellation, host callbacks or trap recovery.
type NativeFunction struct {
	code  *machine.Code
	arity int
}

// JITAvailable reports platform support; QBE and Keystone are still required.
func JITAvailable() bool { return machine.Available() }

// BuildJIT compiles a resolved source graph and installs position-independent
// code in a read/execute mapping. Only the i64/bool QBE subset is supported.
// The entry accepts up to eight scalar arguments. Emit must be empty or bin and
// Target must be empty or amd64_sysv. The caller must Close the returned handle.
func BuildJIT(ctx context.Context, file *fe.File, options NativeOptions) (*NativeFunction, error) {
	if ctx == nil {
		return nil, fmt.Errorf("jit: missing build context")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if options.Emit != "" && options.Emit != "bin" {
		return nil, fmt.Errorf("jit: unsupported artifact %q", options.Emit)
	}
	if options.Target != "" && options.Target != "amd64_sysv" {
		return nil, fmt.Errorf("jit: unsupported target %q", options.Target)
	}
	p, err := EmitQBE(file)
	if err != nil {
		return nil, err
	}
	if options.Entry == "" {
		options.Entry = "main"
	}
	sig, ok := p.Signatures[options.Entry]
	if !ok {
		return nil, fmt.Errorf("jit: undefined entry %q", options.Entry)
	}
	if len(sig.Params) > 8 {
		return nil, fmt.Errorf("jit: entry supports at most eight arguments")
	}
	if !machine.Available() {
		return nil, fmt.Errorf("jit: executable mappings require Linux amd64 with CGO")
	}

	// A uniform trampoline keeps the C call's actual and declared ABI identical.
	// Normalize i64 bool inputs before narrowing to QBE's 32-bit boolean ABI.
	var wrapper strings.Builder
	var params, args []string
	for i := 0; i < 8; i++ {
		params = append(params, fmt.Sprintf("l %%a%d", i))
	}
	symbol := fmt.Sprintf("ig_fn_%d", len(p.Symbols))
	fmt.Fprintf(&wrapper, "function l $%s(%s) {\n@start\n", symbol, strings.Join(params, ", "))
	for i, typ := range sig.Params {
		name := fmt.Sprintf("%%a%d", i)
		if typ == Bool {
			name = fmt.Sprintf("%%b%d", i)
			fmt.Fprintf(&wrapper, "\t%s =w cnel %%a%d, 0\n", name, i)
		}
		args = append(args, typ.qbe()+" "+name)
	}
	fmt.Fprintf(&wrapper, "\t%%r =%s call $%s(%s)\n", sig.Result.qbe(), p.Symbols[options.Entry], strings.Join(args, ", "))
	if sig.Result == Bool {
		wrapper.WriteString("\t%v =l extuw %r\n\tret %v\n}\n")
	} else {
		wrapper.WriteString("\tret %r\n}\n")
	}
	p.IL += wrapper.String()
	// Source names cannot contain a dot, so this private key cannot collide.
	const key = ".jit.entry"
	p.Symbols[key] = symbol
	p.Signatures[key] = Signature{Params: []ScalarType{Int64, Int64, Int64, Int64, Int64, Int64, Int64, Int64}, Result: Int64}
	options.Entry, options.Emit, options.Target = key, "bin", "amd64_sysv"
	data, err := p.Build(ctx, options)
	if err != nil {
		return nil, err
	}
	code, err := machine.Load(data)
	if err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		code.Close()
		return nil, err
	}
	return &NativeFunction{code: code, arity: len(sig.Params)}, nil
}

func (f *NativeFunction) Call(args ...int64) (int64, error) {
	if f == nil {
		return 0, fmt.Errorf("jit: missing function")
	}
	if len(args) != f.arity {
		return 0, fmt.Errorf("jit: expected %d arguments, got %d", f.arity, len(args))
	}
	var values [8]int64
	copy(values[:], args)
	return f.code.Call(values)
}

func (f *NativeFunction) Close() error {
	if f == nil {
		return nil
	}
	return f.code.Close()
}
