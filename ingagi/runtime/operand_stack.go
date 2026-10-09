package runtime

import "fmt"

// Operand stack for expression evaluation. Values are copied by word;
// heap objects (arrays, structs) are shared by pointer.
type operandStack struct {
	slots []Value
}

// newOperandStack returns a stack with preallocated capacity.
func newOperandStack(cap int) *operandStack {
	return &operandStack{slots: make([]Value, 0, cap)}
}

func (s *operandStack) push(v Value) { s.slots = append(s.slots, v) }

func (s *operandStack) pop() Value {
	if len(s.slots) == 0 {
		return NilValue
	}
	v := s.slots[len(s.slots)-1]
	s.slots = s.slots[:len(s.slots)-1]
	return v
}

func (s *operandStack) peek() Value {
	if len(s.slots) == 0 {
		return NilValue
	}
	return s.slots[len(s.slots)-1]
}

func (s *operandStack) depth() int { return len(s.slots) }

func (s *operandStack) reset(n int) {
	if n < 0 {
		n = 0
	}
	if n < len(s.slots) {
		s.slots = s.slots[:n]
	}
}

// ---------------------------------------------------------------------------
// Call stack
// ---------------------------------------------------------------------------

// frame is one activation: a function invocation with its own locals,
// operand base, return address and deferred calls.
type frame struct {
	name     string
	ret      int // return pc (-1 = halt on return)
	base     int // operand stack base
	iterBase int // first iterator owned by this activation
	locals   []Value
	up       []Value // captured upvalues (by value)
	retVal   Value   // stashed return value while deferred calls run
	deferred []int   // deferred call PCs (LIFO at return)
}

// callStack tracks activations with a hard depth limit so runaway
// recursion fails with a diagnostic instead of exhausting memory.
type callStack struct {
	frames []frame
	limit  int
}

const defaultCallLimit = 4096

func newCallStack() *callStack { return &callStack{limit: defaultCallLimit} }

func (c *callStack) push(f frame) error {
	if len(c.frames) >= c.limit {
		return fmt.Errorf("ingagi: stack overflow (call depth > %d)", c.limit)
	}
	c.frames = append(c.frames, f)
	return nil
}

func (c *callStack) pop() frame {
	f := c.frames[len(c.frames)-1]
	c.frames = c.frames[:len(c.frames)-1]
	return f
}

func (c *callStack) top() *frame {
	if len(c.frames) == 0 {
		return nil
	}
	return &c.frames[len(c.frames)-1]
}

func (c *callStack) depth() int { return len(c.frames) }
