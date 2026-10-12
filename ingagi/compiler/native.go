package compiler

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"strconv"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"github.com/Chubek/JoltFX/ingagi/internal/keystone"
)

// NativeOptions selects an artifact and the external tool locations. Empty
// paths use INGAGI_QBE (then PATH's qbe) and INGAGI_KEYSTONE_LIBRARY respectively.
type NativeOptions struct {
	Emit            string // qbe, asm, obj (default), bin, wasm
	Target          string // QBE architecture, or WASM/wasm32
	Entry           string // source function exposed as ingagi_entry; defaults to main
	QBE             string
	KeystoneLibrary string
}

// BuildNative selects a native emitter after the source graph has been resolved.
func BuildNative(ctx context.Context, file *fe.File, options NativeOptions) ([]byte, error) {
	if ctx == nil {
		return nil, fmt.Errorf("native: missing build context")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if options.Emit == "wasm" || options.Target == "WASM" || options.Target == "wasm32" {
		if options.Emit != "" && options.Emit != "wasm" {
			return nil, fmt.Errorf("wasm: unsupported artifact %q", options.Emit)
		}
		if options.Target != "" && options.Target != "WASM" && options.Target != "wasm32" {
			return nil, fmt.Errorf("wasm: unsupported target %q", options.Target)
		}
		spec, err := LookupTarget("WASM")
		if err != nil {
			return nil, err
		}
		if err := spec.RequireArtifact("wasm"); err != nil {
			return nil, err
		}
		data, err := EmitWASM(file, options.Entry)
		if err == nil {
			err = ctx.Err()
		}
		return data, err
	}
	p, err := EmitQBE(file)
	if err != nil {
		return nil, err
	}
	return p.Build(ctx, options)
}

// Build generates an artifact without writing files or invoking a linker.
// QBE emits assembly; Keystone alone assembles machine code. ELF wrapping is
// performed in Go and introduces no relocations or runtime dependencies.
func (p *QBEProgram) Build(ctx context.Context, options NativeOptions) ([]byte, error) {
	if p == nil {
		return nil, fmt.Errorf("native: missing QBE program")
	}
	if ctx == nil {
		return nil, fmt.Errorf("native: missing build context")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if options.Emit == "" {
		options.Emit = "obj"
	}
	capabilities, err := LookupTarget("QBE")
	if err != nil {
		return nil, err
	}
	if err := capabilities.RequireArtifact(options.Emit); err != nil {
		return nil, err
	}
	if options.Target == "" {
		options.Target = "amd64_sysv"
	}
	if options.Entry == "" {
		options.Entry = "main"
	}
	if options.Emit != "qbe" && options.Emit != "asm" && options.Emit != "obj" && options.Emit != "bin" {
		return nil, fmt.Errorf("native: unsupported output format %q", options.Emit)
	}
	switch options.Target {
	case "amd64_sysv", "amd64_apple", "arm64", "arm64_apple", "rv64":
	default:
		return nil, fmt.Errorf("native: unsupported QBE target %q", options.Target)
	}
	entry, ok := p.Symbols[options.Entry]
	if !ok {
		return nil, fmt.Errorf("native: undefined entry %q", options.Entry)
	}
	if !nativeSymbol(entry) {
		return nil, fmt.Errorf("native: invalid entry symbol %q", entry)
	}
	sig, ok := p.Signatures[options.Entry]
	if !ok {
		return nil, fmt.Errorf("native: missing entry signature")
	}
	for _, typ := range append([]ScalarType{sig.Result}, sig.Params...) {
		if typ != Int64 && typ != Bool {
			return nil, fmt.Errorf("native: unsupported ABI type %q", typ)
		}
	}
	il := p.IL
	// Textual artifacts carry the same public C ABI entry as the ELF artifact.
	if options.Emit == "qbe" || options.Emit == "asm" {
		sig := p.Signatures[options.Entry]
		var args []string
		for i, typ := range sig.Params {
			args = append(args, fmt.Sprintf("%s %%a%d", typ.qbe(), i))
		}
		il += fmt.Sprintf("export function %s $ingagi_entry(%s) {\n@start\n\t%%r =%s call $%s(%s)\n\tret %%r\n}\n", sig.Result.qbe(), strings.Join(args, ", "), sig.Result.qbe(), entry, strings.Join(args, ", "))
	}
	if options.Emit == "qbe" {
		return []byte(il), nil
	}
	if options.Emit != "asm" && options.Target != "amd64_sysv" {
		return nil, fmt.Errorf("native: Keystone object/bin export currently supports amd64_sysv only")
	}
	qbe := options.QBE
	if qbe == "" {
		qbe = os.Getenv("INGAGI_QBE")
	}
	if qbe == "" {
		qbe = "qbe"
	}
	cmd := exec.CommandContext(ctx, qbe, "-t", options.Target)
	cmd.Stdin = strings.NewReader(il)
	var assembly, diagnostic bytes.Buffer
	cmd.Stdout, cmd.Stderr = &assembly, &diagnostic
	if err := cmd.Run(); err != nil {
		return nil, fmt.Errorf("qbe: %w: %s", err, strings.TrimSpace(diagnostic.String()))
	}
	if options.Emit == "asm" {
		return assembly.Bytes(), nil
	}
	source, err := keystoneAssembly(assembly.String(), entry)
	if err != nil {
		return nil, err
	}
	library := options.KeystoneLibrary
	if library == "" {
		library = os.Getenv("INGAGI_KEYSTONE_LIBRARY")
	}
	code, err := keystone.Assemble(library, source)
	if err != nil {
		return nil, err
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	if options.Emit == "bin" {
		return code, nil
	}
	return elfObject(code), nil
}

func nativeSymbol(name string) bool {
	if name == "" {
		return false
	}
	for i, c := range name {
		if c == '_' || c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || i > 0 && c >= '0' && c <= '9' {
			continue
		}
		return false
	}
	return true
}

// keystoneAssembly accepts the text-only QBE output generated by our scalar
// subset. Metadata/alignment directives do not change semantics; unknown data
// or relocation directives are rejected instead of silently losing sections.
func keystoneAssembly(assembly, entry string) (string, error) {
	var out strings.Builder
	// Keystone 0.9 predates CET mnemonics. Preserve QBE's ENDBR64 instruction
	// using its exact encoding, including at our externally callable entry stub.
	fmt.Fprintf(&out, ".byte 0xf3, 0x0f, 0x1e, 0xfa\njmp %s\n", entry)
	found := false
	for _, raw := range strings.Split(assembly, "\n") {
		line := strings.TrimSpace(raw)
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, "/*") {
			continue
		}
		if line == entry+":" {
			found = true
		}
		if line == "endbr64" {
			fmt.Fprintln(&out, ".byte 0xf3, 0x0f, 0x1e, 0xfa")
			continue
		}
		if strings.HasPrefix(line, ".") && !strings.HasSuffix(line, ":") {
			fields := strings.Fields(line)
			switch fields[0] {
			case ".text", ".p2align", ".type", ".size", ".globl", ".balign":
				continue
			case ".section":
				if strings.HasPrefix(line, ".section .note.GNU-stack,") {
					continue
				}
			}
			return "", fmt.Errorf("keystone: unsupported QBE assembly directive %q", line)
		}
		fmt.Fprintln(&out, explicitAssemblyNumbers(line))
	}
	if !found {
		return "", fmt.Errorf("keystone: QBE assembly is missing entry label %q", entry)
	}
	return out.String(), nil
}

// Keystone's AT&T parser treats unprefixed numbers as hexadecimal, whereas QBE
// emits decimal immediates/displacements. Make the radix explicit without
// changing register names, symbols or pre-existing hexadecimal literals.
func explicitAssemblyNumbers(line string) string {
	ident := func(b byte) bool {
		return b >= 'a' && b <= 'z' || b >= 'A' && b <= 'Z' || b >= '0' && b <= '9' || b == '_' || b == '.' || b == '%'
	}
	var out strings.Builder
	for i := 0; i < len(line); {
		if line[i] >= '0' && line[i] <= '9' && (i == 0 || !ident(line[i-1])) {
			j := i + 1
			for j < len(line) && line[j] >= '0' && line[j] <= '9' {
				j++
			}
			if j == len(line) || !ident(line[j]) {
				if n, err := strconv.ParseUint(line[i:j], 10, 64); err == nil {
					out.WriteString("0x" + strconv.FormatUint(n, 16))
					i = j
					continue
				}
			}
		}
		out.WriteByte(line[i])
		i++
	}
	return out.String()
}
