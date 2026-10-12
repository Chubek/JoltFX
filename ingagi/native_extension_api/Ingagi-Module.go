// Package native_extension_api implements the scalar host contract shared by
// the C embedding ABI and language bindings. A Session serializes VM access and
// rejects concurrent/reentrant calls with Busy instead of deadlocking.
package native_extension_api

import (
	"errors"
	"fmt"
	"io"
	"sync"

	"github.com/Chubek/JoltFX/ingagi"
	"github.com/Chubek/JoltFX/ingagi/ecs"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

const ABIVersion uint32 = 0x010000 // 1.0.0
const MaxArgs = 256
const MaxSource = 4 << 20

type Status int32

const (
	OK Status = iota
	InvalidArgument
	InvalidHandle
	CompileError
	RuntimeError
	Unsupported
	Busy
	InternalError
)

type Kind uint32

const (
	Nil Kind = iota
	Bool
	Int
	Float
)

type Value struct {
	Kind    Kind
	Integer int64
	Real    float64
}
type Function struct {
	Name  string
	Arity uint32
	Call  func([]Value) (Value, error)
}
type failure struct {
	code    Status
	message string
}

func (e *failure) Error() string             { return e.message }
func fail(code Status, message string) error { return &failure{code, message} }
func ErrorCode(err error) Status {
	if err == nil {
		return OK
	}
	var e *failure
	if errors.As(err, &e) {
		return e.code
	}
	return InternalError
}

type Session struct {
	mu        sync.Mutex
	closed    bool
	vm        *rt.VM
	world     *ecs.World
	budget    rt.Budget
	functions map[string]Function
}

// New creates a private ECS world and defaults zero instructions to one million.
func New(instructions uint64) *Session {
	if instructions == 0 {
		instructions = 1000000
	}
	s := &Session{world: ecs.NewWorld(), budget: rt.Budget{MaxInstrs: instructions, MaxFrames: 256}, functions: map[string]Function{}}
	s.vm = s.newVM()
	return s
}
func (s *Session) lock() error {
	if s == nil {
		return fail(InvalidHandle, "nil session")
	}
	if !s.mu.TryLock() {
		return fail(Busy, "session is busy (concurrent or reentrant call)")
	}
	if s.closed {
		s.mu.Unlock()
		return fail(InvalidHandle, "session is closed")
	}
	return nil
}
func (s *Session) Close() error {
	if err := s.lock(); err != nil {
		return err
	}
	defer s.mu.Unlock()
	s.closed = true
	s.vm = nil
	s.world = nil
	s.functions = nil
	return nil
}
func (s *Session) newVM() *rt.VM {
	vm := rt.New()
	vm.SetWorld(s.world)
	vm.SetBudget(s.budget)
	vm.SetStdout(io.Discard)
	for _, fn := range s.functions {
		install(vm, fn)
	}
	return vm
}
func validName(name string) bool {
	if len(name) == 0 || len(name) > 255 {
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

// Register copies a module's descriptors and validates the entire batch before
// installing anything. Closures/callback code must remain alive until Close.
func (s *Session) Register(functions []Function) error {
	if err := s.lock(); err != nil {
		return err
	}
	defer s.mu.Unlock()
	if len(functions) == 0 || len(functions) > MaxArgs {
		return fail(InvalidArgument, "module needs 1..256 functions")
	}
	seen := map[string]bool{}
	for _, fn := range functions {
		_, exists := s.functions[fn.Name]
		if !validName(fn.Name) || fn.Call == nil || fn.Arity > MaxArgs || exists || seen[fn.Name] {
			return fail(InvalidArgument, "invalid or duplicate function: "+fn.Name)
		}
		seen[fn.Name] = true
	}
	for _, fn := range functions {
		s.functions[fn.Name] = fn
		install(s.vm, fn)
	}
	return nil
}
func install(vm *rt.VM, fn Function) {
	vm.RegisterHostFn(fn.Name, func(_ *rt.VM, args []rt.Value) (rt.Value, error) {
		if len(args) != int(fn.Arity) {
			return rt.NilValue, fmt.Errorf("%s expects %d arguments", fn.Name, fn.Arity)
		}
		values := make([]Value, len(args))
		for i, a := range args {
			v, err := fromRuntime(a)
			if err != nil {
				return rt.NilValue, err
			}
			values[i] = v
		}
		v, err := fn.Call(values)
		if err != nil {
			return rt.NilValue, err
		}
		return toRuntime(v)
	})
}

// Load compiles and initializes a replacement VM before installing it. Callback
// and shared-world side effects from a failing initializer cannot be rolled back.
func (s *Session) Load(source string) error {
	if err := s.lock(); err != nil {
		return err
	}
	defer s.mu.Unlock()
	if len(source) > MaxSource {
		return fail(InvalidArgument, "source exceeds 4 MiB")
	}
	m, err := ingagi.Compile(source)
	if err != nil {
		return fail(CompileError, err.Error())
	}
	vm := s.newVM()
	if err := vm.LoadModule(m); err != nil {
		return fail(RuntimeError, err.Error())
	}
	s.vm = vm
	return nil
}
func (s *Session) Call(name string, args []Value) (Value, error) {
	if err := s.lock(); err != nil {
		return Value{}, err
	}
	defer s.mu.Unlock()
	if !validName(name) || len(args) > MaxArgs {
		return Value{}, fail(InvalidArgument, "invalid entry name or argument count")
	}
	values := make([]rt.Value, len(args))
	for i, a := range args {
		v, err := toRuntime(a)
		if err != nil {
			return Value{}, err
		}
		values[i] = v
	}
	v, err := s.vm.Call(name, values...)
	if err != nil {
		return Value{}, fail(RuntimeError, err.Error())
	}
	return fromRuntime(v)
}
func toRuntime(v Value) (rt.Value, error) {
	switch v.Kind {
	case Nil:
		return rt.NilValue, nil
	case Bool:
		if v.Integer == 0 || v.Integer == 1 {
			return rt.BoolValue(v.Integer == 1), nil
		}
	case Int:
		return rt.IntValue(v.Integer), nil
	case Float:
		return rt.FloatValue(v.Real), nil
	}
	return rt.NilValue, fail(InvalidArgument, "invalid scalar kind or boolean payload")
}
func fromRuntime(v rt.Value) (Value, error) {
	switch v.K {
	case rt.KNil:
		return Value{}, nil
	case rt.KBool:
		n := int64(0)
		if v.B {
			n = 1
		}
		return Value{Kind: Bool, Integer: n}, nil
	case rt.KInt:
		return Value{Kind: Int, Integer: v.I}, nil
	case rt.KFloat:
		return Value{Kind: Float, Real: v.F}, nil
	}
	return Value{}, fail(Unsupported, "native ABI v1 cannot marshal "+v.K.String())
}
