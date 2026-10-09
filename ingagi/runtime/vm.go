package runtime

import (
	"fmt"
	"io"
	"os"
	"strings"

	"github.com/Chubek/JoltFX/ingagi/ecs"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// VM executes Ingagi modules. A VM is cheap to create and safe to reuse:
// globals persist across calls, the world is injectable, and budgets bound
// every run. The zero value is not usable; construct with New.
type VM struct {
	tape         *Tape
	globals      map[string]Value
	funcs        map[string]int    // entry name -> pc
	params       map[string]int    // entry name -> param count
	methods      map[string]string // Type::method -> entry name
	types        map[string]*TypeInfo
	defs         map[string]map[string]Value // type -> field -> default
	systems      map[string]map[string]int   // system -> hook -> pc
	evHooks      map[string][]int            // event name -> hook pcs
	hooksArg     map[int]string              // hook pc -> event param type
	observers    []observer
	events       []Event
	bindings     []RenderBinding
	prefabs      map[string]bool
	world        *ecs.World
	builtins     map[string]func(*VM, []Value) (Value, error)
	hostFns      map[string]func(*VM, []Value) (Value, error)
	caches       map[int]*inlineCache
	profiles     map[int]*opProfile
	stdout       io.Writer
	budget       Budget
	activeBudget Budget
	execCount    uint64
	emitDepth    int
	// AOT side tables (populated by LoadModule/LoadDecoded for Encode).
	aotHooks   []HookInfo
	aotSystems []string
	aotPrefabs []string
	aotStrings map[string][]string

	stack  *operandStack
	frames *callStack
	iters  []foreachIter
}

// Event is a published typed event awaiting dispatch.
type Event struct {
	Type   string
	Fields map[string]Value
}

// RenderBinding wires a pipeline/stage over a query for the engine.
type RenderBinding struct {
	Pipeline string
	Stage    string
	With     []string
	Without  []string
}

// observer is an inline `on` registration.
type observer struct {
	kind  int // 0 add, 1 remove, 2 set
	comp  string
	entry int
	up    []Value
}

// foreachIter is one live `for each` query.
type foreachIter struct {
	entities []ecs.EntityID
	rows     [][]Value // per entity, per binding snapshot
	mut      []bool
	alias    []string
	comp     []string
	slots    []int32 // local slot per binding for fill/write-back
	world    *ecs.World
	idx      int // -1 before first row; flushed through idx
}

// inlineCache is a monomorphic call-site cache (see jit.go).
type inlineCache struct {
	kind   int
	key    string
	method string
	entry  string
	hit    int
}

// opProfile counts executions for the specializing tier.
type opProfile struct {
	count int
	seen  int // bitmask of observed operand kinds
}

// New returns an empty VM with the standard builtins registered.
func New() *VM {
	vm := &VM{
		globals:  map[string]Value{},
		funcs:    map[string]int{},
		params:   map[string]int{},
		methods:  map[string]string{},
		types:    map[string]*TypeInfo{},
		defs:     map[string]map[string]Value{},
		systems:  map[string]map[string]int{},
		evHooks:  map[string][]int{},
		hooksArg: map[int]string{},
		builtins: map[string]func(*VM, []Value) (Value, error){},
		hostFns:  map[string]func(*VM, []Value) (Value, error){},
		caches:   map[int]*inlineCache{},
		profiles: map[int]*opProfile{},
		stdout:   os.Stdout,
		stack:    newOperandStack(256),
		frames:   newCallStack(),
	}
	registerBuiltins(vm)
	return vm
}

// SetStdout redirects script `print` output (engine consoles, tests).
func (vm *VM) SetStdout(w io.Writer) { vm.stdout = w }

// SetBudget sets the default budget for subsequent calls.
func (vm *VM) SetBudget(b Budget) { vm.budget = b }

// SetWorld sets the default world for builtins that omit it.
func (vm *VM) SetWorld(w *ecs.World) { vm.world = w }

// World returns the default world (may be nil).
func (vm *VM) World() *ecs.World { return vm.world }

// RegisterHostFn exposes a Go function as `lib:name` extern or builtin.
func (vm *VM) RegisterHostFn(name string, fn func(*VM, []Value) (Value, error)) {
	vm.hostFns[name] = fn
}

// LoadModule links a compiled module: entries, types, systems, hooks.
// The world's entities survive reloads; code and globals are replaced.
func (vm *VM) LoadModule(m *Module) error {
	vm.tape = m.Tape
	vm.funcs = map[string]int{}
	vm.methods = map[string]string{}
	vm.types = m.Types
	vm.defs = map[string]map[string]Value{}
	vm.systems = map[string]map[string]int{}
	vm.evHooks = map[string][]int{}
	vm.hooksArg = map[int]string{}
	vm.observers = nil
	vm.events = nil
	vm.bindings = nil
	vm.prefabs = map[string]bool{}
	for name := range m.Entities {
		vm.prefabs[name] = true
	}
	vm.rememberModule(m)
	for name, pc := range m.Tape.Entry {
		vm.funcs[name] = pc
	}
	for name, n := range m.Params {
		vm.params[name] = n
	}
	for name, ti := range m.Types {
		for method, entry := range ti.Methods {
			vm.methods[name+"::"+method] = entry
		}
	}
	// Run <init> to evaluate consts, resources and defaults.
	if pc, ok := vm.funcs["<init>"]; ok {
		if _, err := vm.callEntry(pc, "<init>", nil, nil, vm.budget); err != nil {
			return err
		}
	}
	// Harvest field defaults.
	for name := range m.Types {
		d := map[string]Value{}
		for k, v := range vm.globals {
			if strings.HasPrefix(k, "default::"+name+"::") {
				d[strings.TrimPrefix(k, "default::"+name+"::")] = v
				delete(vm.globals, k)
			}
		}
		vm.defs[name] = d
	}
	// Resources and consts stay in globals; functions become closures.
	for name := range vm.funcs {
		if _, ok := vm.globals[name]; !ok {
			vm.globals[name] = Value{K: KClosure, O: &ClosureObj{Name: name, Fn: 0}}
		}
	}
	// Enum variants become qualified globals (bare names via suffix match).
	for name, ti := range m.Types {
		if ti.Kind != "enum" {
			continue
		}
		for _, v := range ti.Variants {
			vm.globals[name+"::"+v] = Value{K: KEnum, O: &EnumObj{Type: name, Variant: v}}
		}
	}
	// Index system hooks from the hook table (AST-free; AOT-safe).
	for _, h := range m.Hooks {
		pc, ok := vm.funcs[h.Entry]
		if !ok {
			continue
		}
		if vm.systems[h.System] == nil {
			vm.systems[h.System] = map[string]int{}
		}
		vm.systems[h.System][h.Kind] = pc
		if h.Kind == "on_event" && h.Event != "" {
			vm.evHooks[h.Event] = append(vm.evHooks[h.Event], pc)
			vm.hooksArg[pc] = h.Event
		}
	}
	return nil
}

// typeLast renders the trailing segment of a type expression for
// on_event parameter matching.
func typeLast(t fe.Type) string {
	switch t := t.(type) {
	case *fe.TypeName:
		return t.Path.Last()
	case *fe.RefType:
		return typeLast(t.Elem)
	case *fe.PtrType:
		return typeLast(t.Elem)
	case *fe.ArrayType:
		return typeLast(t.Elem)
	default:
		return ""
	}
}

// Call invokes a named function with args under the default budget.
func (vm *VM) Call(name string, args ...Value) (Value, error) {
	return vm.CallBudget(vm.budget, name, args...)
}

// CallBudget invokes a named function with an explicit budget.
func (vm *VM) CallBudget(b Budget, name string, args ...Value) (Value, error) {
	pc, ok := vm.funcs[name]
	if !ok {
		if _, ok := vm.builtins[name]; ok {
			return vm.builtins[name](vm, args)
		}
		return NilValue, runtimeErrorf("undefined function %q", name)
	}
	return vm.callEntry(pc, name, nil, args, b)
}

// RunHook runs a system hook by system and kind (update, fixed_update…).
func (vm *VM) RunHook(system, kind string, args ...Value) (Value, error) {
	hooks, ok := vm.systems[system]
	if !ok {
		return NilValue, runtimeErrorf("unknown system %q", system)
	}
	pc, ok := hooks[kind]
	if !ok {
		return NilValue, runtimeErrorf("system %q has no %q hook", system, kind)
	}
	return vm.callEntry(pc, system+"::"+kind, nil, args, vm.budget)
}

// Events drains queued events (oldest first).
func (vm *VM) Events() []Event { return vm.events }

// DrainEvents returns queued events and clears the queue.
func (vm *VM) DrainEvents() []Event {
	e := vm.events
	vm.events = nil
	return e
}

// Bindings returns renderer edges recorded by `bind` statements.
func (vm *VM) Bindings() []RenderBinding { return vm.bindings }

// Disassemble renders the loaded tape for tooling.
func (vm *VM) Disassemble() []string {
	if vm.tape == nil {
		return nil
	}
	return vm.tape.Disassemble()
}

// trace builds a call trace for diagnostics.
func (vm *VM) trace() []string {
	var out []string
	for i := len(vm.frames.frames) - 1; i >= 0; i-- {
		out = append(out, vm.frames.frames[i].name)
	}
	return out
}

func (vm *VM) fail(format string, args ...any) *RuntimeError {
	return &RuntimeError{Msg: fmt.Sprintf(format, args...), Trace: vm.trace()}
}
