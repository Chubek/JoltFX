package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/Chubek/JoltFX/ingagi"
	"github.com/Chubek/JoltFX/ingagi/compiler"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"github.com/Chubek/JoltFX/ingagi/module_system"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
	"github.com/Chubek/JoltFX/ingagi/sema"
	"github.com/Chubek/JoltFX/ingagi/shader"
)

func runCLI(args []string, stdout, stderr io.Writer) int {
	usage := func() {
		fmt.Fprintln(stderr, "usage: ingagi {run|check|dump} [-entry main] [-budget 1000000] FILE\n       ingagi build [-emit obj|bin|asm|qbe|wasm] [-target amd64_sysv|wasm32] [-entry main] -o OUTPUT FILE\n       ingagi shader [-shader NAME] [-stage compute] [-emit glsl|spirv] -o OUTPUT FILE\n       ingagi targets")
	}
	if len(args) == 0 {
		usage()
		return 2
	}
	command := args[0]
	if command == "targets" {
		if len(args) != 1 {
			fmt.Fprintln(stderr, "usage: ingagi targets")
			return 2
		}
		specs, err := compiler.Targets()
		if err != nil {
			fmt.Fprintln(stderr, err)
			return 1
		}
		for _, spec := range specs {
			fmt.Fprintf(stdout, "%s\t%s\t%s\t%v\n", spec.ID, spec.Status, spec.Kind, spec.Artifacts)
		}
		return 0
	}
	if command != "run" && command != "check" && command != "dump" && command != "build" && command != "shader" {
		usage()
		return 2
	}
	flags := flag.NewFlagSet("ingagi "+command, flag.ContinueOnError)
	flags.SetOutput(stderr)
	entry := flags.String("entry", "main", "function to invoke or export")
	budget := flags.Uint64("budget", 1000000, "instruction limit per VM invocation; 0 means unlimited")
	var emit, target, output, qbe, keystone string
	var shaderName, stage, glslang, spirvCross, naga string
	if command == "build" {
		flags.StringVar(&emit, "emit", "", "output format: obj (default), bin, asm, qbe, wasm; wasm32 defaults to wasm")
		flags.StringVar(&target, "target", "", "QBE architecture (default amd64_sysv), or wasm32")
		flags.StringVar(&output, "o", "", "output path (required)")
		flags.StringVar(&qbe, "qbe", "", "QBE executable (default INGAGI_QBE or PATH)")
		flags.StringVar(&keystone, "keystone", "", "libkeystone.so path (default INGAGI_KEYSTONE_LIBRARY)")
	}
	if command == "shader" {
		flags.StringVar(&emit, "emit", "spirv", "output format: glsl, spirv, hlsl, glsl-opengl, glsl-es, msl, wgsl")
		flags.StringVar(&shaderName, "shader", "", "shader declaration (required when more than one exists)")
		flags.StringVar(&stage, "stage", "compute", "shader stage: vertex, fragment, compute")
		flags.StringVar(&glslang, "glslang", "", "glslangValidator executable")
		flags.StringVar(&spirvCross, "spirv-cross", "", "SPIRV-Cross executable")
		flags.StringVar(&naga, "naga", "", "Naga shader compiler executable")
		flags.StringVar(&output, "o", "", "output path (required)")
	}
	if err := flags.Parse(args[1:]); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return 0
		}
		return 2
	}
	if flags.NArg() != 1 {
		usage()
		return 2
	}
	path := flags.Arg(0)
	if command == "build" || command == "shader" {
		if output == "" {
			fmt.Fprintf(stderr, "%s requires -o OUTPUT\n", command)
			return 2
		}
		inAbs, _ := filepath.Abs(path)
		outAbs, _ := filepath.Abs(output)
		inStat, inErr := os.Stat(path)
		outStat, outErr := os.Stat(output)
		if inAbs == outAbs || inErr == nil && outErr == nil && os.SameFile(inStat, outStat) {
			fmt.Fprintln(stderr, "build output must not replace the input source")
			return 2
		}
	}
	fail := func(err error) int { fmt.Fprintf(stderr, "%s: %v\n", path, err); return 1 }
	if command == "shader" {
		f, err := (&module_system.Loader{FS: os.DirFS(filepath.Dir(path))}).Load(filepath.Base(path))
		if err != nil {
			return fail(err)
		}
		var selected *fe.ShaderDecl
		for _, d := range f.Decls {
			if s, ok := d.(*fe.ShaderDecl); ok && (shaderName == "" || s.Name == shaderName) {
				if selected != nil {
					return fail(fmt.Errorf("select a shader with -shader NAME"))
				}
				selected = s
			}
		}
		if selected == nil {
			return fail(fmt.Errorf("shader %q not found", shaderName))
		}
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()
		data, err := shader.CompileArtifact(ctx, selected, stage, emit, shader.ToolOptions{GLSLang: glslang, SPIRVCross: spirvCross, Naga: naga})
		if err != nil {
			return fail(err)
		}
		if err := writeArtifact(output, data); err != nil {
			return fail(err)
		}
		return 0
	}
	if command == "build" {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()
		data, err := ingagi.CompileNativeFile(ctx, os.DirFS(filepath.Dir(path)), filepath.Base(path), ingagi.NativeOptions{Emit: emit, Target: target, Entry: *entry, QBE: qbe, KeystoneLibrary: keystone})
		if err != nil {
			return fail(err)
		}
		if err := writeArtifact(output, data); err != nil {
			return fail(err)
		}
		return 0
	}
	if command == "check" {
		f, err := (&module_system.Loader{FS: os.DirFS(filepath.Dir(path))}).Load(filepath.Base(path))
		if err != nil {
			return fail(err)
		}
		ds := sema.Check(f, sema.DefaultOptions())
		for _, d := range ds {
			fmt.Fprintf(stderr, "%s: %s\n", path, d)
		}
		if sema.HasErrors(ds) {
			return 1
		}
		m, errs := rt.Compile(f)
		if len(errs) > 0 {
			for _, err := range errs {
				fmt.Fprintf(stderr, "%s: %s\n", path, err)
			}
			return 1
		}
		if err := rt.Validate(m); err != nil {
			return fail(err)
		}
		return 0
	}
	m, err := ingagi.CompileFile(os.DirFS(filepath.Dir(path)), filepath.Base(path))
	if err != nil {
		return fail(err)
	}
	switch command {
	case "dump":
		for _, line := range m.Tape.Disassemble() {
			fmt.Fprintln(stdout, line)
		}
		return 0
	}
	vm := rt.New()
	vm.SetStdout(stdout)
	vm.SetBudget(rt.Budget{MaxInstrs: *budget})
	if err := vm.LoadModule(m); err != nil {
		return fail(err)
	}
	v, err := vm.Call(*entry)
	if err != nil {
		return fail(err)
	}
	if !v.IsNil() {
		fmt.Fprintln(stdout, v)
	}
	return 0
}

func writeArtifact(path string, data []byte) error {
	f, err := os.CreateTemp(filepath.Dir(path), ".ingagi-artifact-*")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	if _, err := f.Write(data); err != nil {
		f.Close()
		return err
	}
	if err := f.Close(); err != nil {
		return err
	}
	return os.Rename(f.Name(), path)
}
