package main

import (
	"errors"
	"flag"
	"fmt"
	"io"
	"os"

	"github.com/Chubek/JoltFX/ingagi"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func runCLI(args []string, stdout, stderr io.Writer) int {
	usage := func() { fmt.Fprintln(stderr, "usage: ingagi {run|check|dump} [-entry main] [-budget 1000000] FILE") }
	if len(args) == 0 {
		usage()
		return 2
	}
	command := args[0]
	if command != "run" && command != "check" && command != "dump" {
		usage()
		return 2
	}
	flags := flag.NewFlagSet("ingagi "+command, flag.ContinueOnError)
	flags.SetOutput(stderr)
	entry := flags.String("entry", "main", "function to invoke (run only)")
	budget := flags.Uint64("budget", 1000000, "instruction limit per VM invocation; 0 means unlimited")
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
	fail := func(err error) int { fmt.Fprintf(stderr, "%s: %v\n", path, err); return 1 }
	source, err := os.ReadFile(path)
	if err != nil {
		return fail(err)
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
