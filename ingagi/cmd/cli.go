package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"github.com/Chubek/JoltFX/ingagi/compiler"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/Chubek/JoltFX/ingagi"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func runCLI(args []string, stdout, stderr io.Writer) int {
	usage := func() {
		fmt.Fprintln(stderr, "usage: ingagi {run|check|dump} [-entry main] [-budget 1000000] FILE\n       ingagi build [-emit obj|bin|asm|qbe] [-target amd64_sysv] [-entry main] -o OUTPUT FILE\n       ingagi targets")
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
	if command != "run" && command != "check" && command != "dump" && command != "build" {
		usage()
		return 2
	}
	flags := flag.NewFlagSet("ingagi "+command, flag.ContinueOnError)
	flags.SetOutput(stderr)
	entry := flags.String("entry", "main", "function to invoke or export")
	budget := flags.Uint64("budget", 1000000, "instruction limit per VM invocation; 0 means unlimited")
	var emit, target, output, qbe, keystone string
	if command == "build" {
		flags.StringVar(&emit, "emit", "obj", "output format: obj, bin, asm, qbe")
		flags.StringVar(&target, "target", "amd64_sysv", "QBE target")
		flags.StringVar(&output, "o", "", "output path (required)")
		flags.StringVar(&qbe, "qbe", "", "QBE executable (default INGAGI_QBE or PATH)")
		flags.StringVar(&keystone, "keystone", "", "libkeystone.so path (default INGAGI_KEYSTONE_LIBRARY)")
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
	if command == "build" {
		if output == "" {
			fmt.Fprintln(stderr, "build requires -o OUTPUT")
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
	source, err := os.ReadFile(path)
	if err != nil {
		return fail(err)
	}
	if command == "build" {
		ctx, cancel := context.WithTimeout(context.Background(), 30*time.Second)
		defer cancel()
		data, err := ingagi.CompileNative(ctx, string(source), ingagi.NativeOptions{Emit: emit, Target: target, Entry: *entry, QBE: qbe, KeystoneLibrary: keystone})
		if err != nil {
			return fail(err)
		}
		if err := writeArtifact(output, data); err != nil {
			return fail(err)
		}
		return 0
	}
	m, err := ingagi.Compile(string(source))
	if err != nil {
		return fail(err)
	}
	switch command {
	case "check":
		return 0
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
