package runtime

import "fmt"

// Execution limits and runtime errors. Instruction and call-depth budgets
// bound bytecode execution; they do not bound host callbacks, allocations or
// wall-clock time and do not constitute a sandbox.

// Budget caps execution resources per call into the VM.
type Budget struct {
	// MaxInstrs bounds executed instructions (0 = unlimited).
	MaxInstrs uint64
	// MaxFrames bounds call depth; 0 uses the default.
	MaxFrames int
}

// Unlimited is an unbounded budget (tooling use).
var Unlimited = Budget{}

// RuntimeError is a script-visible failure with an optional call trace.
type RuntimeError struct {
	Msg   string
	Trace []string
}

func (e *RuntimeError) Error() string {
	if len(e.Trace) == 0 {
		return "ingagi: " + e.Msg
	}
	s := "ingagi: " + e.Msg
	for i := len(e.Trace) - 1; i >= 0; i-- {
		s += "\n  at " + e.Trace[i]
	}
	return s
}

func runtimeErrorf(format string, args ...any) *RuntimeError {
	return &RuntimeError{Msg: fmt.Sprintf(format, args...)}
}
