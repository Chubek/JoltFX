package compiler

import (
	"fmt"
	"strconv"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// ScalarType is the current native subset's C ABI type. Native integers are
// signed 64-bit values; bool crosses the ABI as a normalized 32-bit integer.
type ScalarType string

const (
	Int64 ScalarType = "i64"
	Bool  ScalarType = "bool"
)

func (t ScalarType) qbe() string {
	if t == Bool {
		return "w"
	}
	return "l"
}

type Signature struct {
	Params []ScalarType
	Result ScalarType
}

// QBEProgram owns IL plus the mapping from source names to private native names.
// Only the selected entry is exported by the object-file writer.
type QBEProgram struct {
	IL         string
	Symbols    map[string]string
	Signatures map[string]Signature
}

// EmitQBE lowers the typed scalar subset. Unsupported constructs are errors;
// they are never silently omitted or replaced with VM calls.
func EmitQBE(file *fe.File) (*QBEProgram, error) {
	if file == nil {
		return nil, fmt.Errorf("qbe: missing source tree")
	}
	p := &QBEProgram{Symbols: map[string]string{}, Signatures: map[string]Signature{}}
	if len(file.Imports) != 0 {
		return nil, fmt.Errorf("%s: qbe: imports are not supported", file.Imports[0].Span)
	}
	var functions []*fe.FnDecl
	for _, decl := range file.Decls {
		fn, ok := decl.(*fe.FnDecl)
		if !ok {
			return nil, fmt.Errorf("%s: qbe: unsupported declaration %T", decl.Pos(), decl)
		}
		if _, exists := p.Symbols[fn.Name]; exists {
			return nil, fmt.Errorf("%s: qbe: duplicate function %q", fn.Span, fn.Name)
		}
		if fn.Body == nil || fn.Async || len(fn.Params) != 0 || len(fn.Attrs) != 0 {
			return nil, fmt.Errorf("%s: qbe: function must have a body and no async, generics or attributes", fn.Span)
		}
		ret, err := nativeType(fn.Ret)
		if err != nil {
			return nil, fmt.Errorf("%s: qbe: explicit return type required: %w", fn.Span, err)
		}
		sig := Signature{Result: ret}
		seen := map[string]bool{}
		for _, a := range fn.Args {
			if seen[a.Name] {
				return nil, fmt.Errorf("%s: qbe: duplicate parameter %q", a.Span, a.Name)
			}
			seen[a.Name] = true
			if a.Default != nil || len(a.Attrs) != 0 {
				return nil, fmt.Errorf("%s: qbe: parameter defaults and attributes are unsupported", a.Span)
			}
			typ, err := nativeType(a.Type)
			if err != nil {
				return nil, fmt.Errorf("%s: qbe: %w", a.Span, err)
			}
			sig.Params = append(sig.Params, typ)
		}
		p.Symbols[fn.Name] = fmt.Sprintf("ig_fn_%d", len(functions))
		p.Signatures[fn.Name] = sig
		functions = append(functions, fn)
	}
	if len(functions) == 0 {
		return nil, fmt.Errorf("qbe: no functions to compile")
	}
	var out strings.Builder
	for _, fn := range functions {
		c := &qbeFunction{program: p, result: p.Signatures[fn.Name].Result}
		c.pushScope()
		var args []string
		for i, a := range fn.Args {
			typ := p.Signatures[fn.Name].Params[i]
			arg := fmt.Sprintf("%%arg%d", i)
			args = append(args, typ.qbe()+" "+arg)
			// Normalize host-supplied boolean arguments at the ABI boundary.
			v := qbeValue{arg, typ}
			if typ == Bool {
				v = c.instruction(Bool, "cnew %s, 0", arg)
			}
			c.declare(a.Name, v, a.Span)
		}
		c.block(fn.Body)
		if !c.terminated {
			c.fail(fn.Span, "function %q can finish without a return", fn.Name)
		}
		if c.err != nil {
			return nil, c.err
		}
		fmt.Fprintf(&out, "function %s $%s(%s) {\n@start\n%s%s}\n\n", c.result.qbe(), p.Symbols[fn.Name], strings.Join(args, ", "), c.allocs.String(), c.body.String())
	}
	p.IL = out.String()
	return p, nil
}

func nativeType(t fe.Type) (ScalarType, error) {
	n, ok := t.(*fe.TypeName)
	if !ok || n.Optional || len(n.Args) != 0 || n.Path.Global || len(n.Path.Parts) != 1 {
		return "", fmt.Errorf("unsupported type (expected i64 or bool)")
	}
	switch n.Path.Last() {
	case "i64":
		return Int64, nil
	case "bool":
		return Bool, nil
	default:
		return "", fmt.Errorf("unsupported type %q (expected i64 or bool)", n.Path.String())
	}
}

type qbeValue struct {
	ref string
	typ ScalarType
}
type qbeLocal struct {
	ptr string
	typ ScalarType
}
type qbeLoop struct{ next, end string }
type qbeFunction struct {
	program      *QBEProgram
	result       ScalarType
	allocs, body strings.Builder
	scopes       []map[string]qbeLocal
	loops        []qbeLoop
	sequence     int
	terminated   bool
	err          error
}

func (c *qbeFunction) fail(sp fe.Span, format string, args ...any) {
	if c.err == nil {
		c.err = fmt.Errorf("%s: qbe: %s", sp, fmt.Sprintf(format, args...))
	}
}
func (c *qbeFunction) fresh(prefix string) string {
	c.sequence++
	return fmt.Sprintf("%s%d", prefix, c.sequence)
}
func (c *qbeFunction) emit(format string, args ...any) {
	fmt.Fprintf(&c.body, "\t"+format+"\n", args...)
}
func (c *qbeFunction) label(name string) { fmt.Fprintf(&c.body, "@%s\n", name); c.terminated = false }
func (c *qbeFunction) jump(name string)  { c.emit("jmp @%s", name); c.terminated = true }
func (c *qbeFunction) instruction(typ ScalarType, format string, args ...any) qbeValue {
	v := qbeValue{c.fresh("%v"), typ}
	c.emit("%s =%s %s", v.ref, typ.qbe(), fmt.Sprintf(format, args...))
	return v
}
func (c *qbeFunction) slot(typ ScalarType) qbeLocal {
	v := qbeLocal{c.fresh("%s"), typ}
	fmt.Fprintf(&c.allocs, "\t%s =l alloc8 8\n", v.ptr)
	return v
}
func (c *qbeFunction) store(slot qbeLocal, value qbeValue) {
	c.emit("store%s %s, %s", slot.typ.qbe(), value.ref, slot.ptr)
}
func (c *qbeFunction) load(slot qbeLocal) qbeValue {
	return c.instruction(slot.typ, "load%s %s", slot.typ.qbe(), slot.ptr)
}
func (c *qbeFunction) pushScope() { c.scopes = append(c.scopes, map[string]qbeLocal{}) }
func (c *qbeFunction) popScope()  { c.scopes = c.scopes[:len(c.scopes)-1] }
func (c *qbeFunction) local(name string) (qbeLocal, bool) {
	for i := len(c.scopes) - 1; i >= 0; i-- {
		if v, ok := c.scopes[i][name]; ok {
			return v, true
		}
	}
	return qbeLocal{}, false
}
func (c *qbeFunction) declare(name string, v qbeValue, sp fe.Span) {
	scope := c.scopes[len(c.scopes)-1]
	if _, ok := scope[name]; ok {
		c.fail(sp, "duplicate local %q", name)
		return
	}
	slot := c.slot(v.typ)
	scope[name] = slot
	c.store(slot, v)
}
func (c *qbeFunction) truth(v qbeValue) qbeValue {
	if v.typ == Bool {
		return v
	}
	return c.instruction(Bool, "cnel %s, 0", v.ref)
}
func (c *qbeFunction) branch(v qbeValue, yes, no string) {
	v = c.truth(v)
	c.emit("jnz %s, @%s, @%s", v.ref, yes, no)
	c.terminated = true
}

func (c *qbeFunction) block(b *fe.Block) {
	if b == nil {
		return
	}
	c.pushScope()
	defer c.popScope()
	for _, s := range b.Stmts {
		if c.terminated { // Still validate unreachable statements in an isolated block.
			c.label(c.fresh("dead"))
			c.stmt(s)
			if !c.terminated {
				c.emit("hlt")
				c.terminated = true
			}
		} else {
			c.stmt(s)
		}
	}
}

func (c *qbeFunction) stmt(s fe.Stmt) {
	switch s := s.(type) {
	case *fe.Block:
		c.block(s)
	case *fe.VarDeclStmt:
		if s.Init == nil {
			c.fail(s.Span, "native local %q requires an initializer", s.Name)
			return
		}
		if len(s.Attrs) != 0 {
			c.fail(s.Span, "local attributes are unsupported")
		}
		v := c.expr(s.Init)
		if s.Type != nil {
			typ, err := nativeType(s.Type)
			if err != nil {
				c.fail(s.Span, "%v", err)
			} else if typ != v.typ {
				c.fail(s.Span, "initializer type %s does not match %s", v.typ, typ)
			}
		}
		c.declare(s.Name, v, s.Span)
	case *fe.ExprStmt:
		c.expr(s.X)
	case *fe.ReturnStmt:
		if s.Value == nil {
			c.fail(s.Span, "return value required")
			return
		}
		v := c.expr(s.Value)
		if v.typ != c.result {
			c.fail(s.Span, "return type %s does not match %s", v.typ, c.result)
		}
		c.emit("ret %s", v.ref)
		c.terminated = true
	case *fe.IfStmt:
		yes, no, end := c.fresh("then"), c.fresh("else"), c.fresh("join")
		c.branch(c.expr(s.Cond), yes, no)
		c.label(yes)
		c.block(s.Then)
		thenReturns := c.terminated
		if !c.terminated {
			c.jump(end)
		}
		c.label(no)
		if s.ElseIf != nil {
			c.stmt(s.ElseIf)
		} else {
			c.block(s.Else)
		}
		elseReturns := c.terminated
		if !c.terminated {
			c.jump(end)
		}
		if thenReturns && elseReturns {
			c.terminated = true
		} else {
			c.label(end)
		}
	case *fe.WhileStmt:
		cond, body, end := c.fresh("cond"), c.fresh("body"), c.fresh("end")
		c.jump(cond)
		c.label(cond)
		c.branch(c.expr(s.Cond), body, end)
		c.loops = append(c.loops, qbeLoop{cond, end})
		c.label(body)
		c.block(s.Body)
		if !c.terminated {
			c.jump(cond)
		}
		c.loops = c.loops[:len(c.loops)-1]
		c.label(end)
	case *fe.LoopStmt:
		body, end := c.fresh("loop"), c.fresh("end")
		c.jump(body)
		c.label(body)
		c.loops = append(c.loops, qbeLoop{body, end})
		c.block(s.Body)
		if !c.terminated {
			c.jump(body)
		}
		c.loops = c.loops[:len(c.loops)-1]
		c.label(end)
	case *fe.BreakStmt:
		if len(c.loops) == 0 {
			c.fail(s.Span, "break outside loop")
			return
		}
		if s.Label != "" || s.Value != nil {
			c.fail(s.Span, "labeled/value breaks are unsupported")
			return
		}
		c.jump(c.loops[len(c.loops)-1].end)
	case *fe.ContinueStmt:
		if len(c.loops) == 0 {
			c.fail(s.Span, "continue outside loop")
			return
		}
		if s.Label != "" {
			c.fail(s.Span, "labeled continue is unsupported")
			return
		}
		c.jump(c.loops[len(c.loops)-1].next)
	default:
		c.fail(s.Pos(), "unsupported statement %T", s)
	}
}

func (c *qbeFunction) expr(e fe.Expr) qbeValue {
	bad := qbeValue{"0", Int64}
	if e == nil {
		c.fail(fe.Span{}, "missing expression")
		return bad
	}
	switch e := e.(type) {
	case *fe.Lit:
		switch e.Kind {
		case fe.LitInt:
			return qbeValue{strconv.FormatInt(e.Int, 10), Int64}
		case fe.LitBool:
			if e.Bool {
				return qbeValue{"1", Bool}
			}
			return qbeValue{"0", Bool}
		default:
			c.fail(e.Span, "unsupported literal (expected i64 or bool)")
		}
	case *fe.IdentExpr:
		if v, ok := c.local(e.Name); ok {
			return c.load(v)
		}
		c.fail(e.Span, "undefined local %q", e.Name)
	case *fe.UnaryExpr:
		v := c.expr(e.X)
		switch e.Op {
		case "!":
			v = c.truth(v)
			return c.instruction(Bool, "ceqw %s, 0", v.ref)
		case "+", "-", "~":
			if v.typ != Int64 {
				c.fail(e.Span, "unary %s requires i64", e.Op)
				return bad
			}
			if e.Op == "+" {
				return v
			}
			if e.Op == "-" {
				return c.instruction(Int64, "sub 0, %s", v.ref)
			}
			return c.instruction(Int64, "xor %s, -1", v.ref)
		default:
			c.fail(e.Span, "unsupported unary operator %q", e.Op)
		}
	case *fe.BinaryExpr:
		if e.Op == "&&" || e.Op == "||" {
			x := c.truth(c.expr(e.X))
			yes, no, end := c.fresh("rhs"), c.fresh("short"), c.fresh("join")
			slot := c.slot(Bool)
			if e.Op == "&&" {
				c.branch(x, yes, no)
			} else {
				c.branch(x, no, yes)
			}
			c.label(yes)
			y := c.truth(c.expr(e.Y))
			c.store(slot, y)
			c.jump(end)
			c.label(no)
			c.store(slot, x)
			c.jump(end)
			c.label(end)
			return c.load(slot)
		}
		left := c.expr(e.X)
		right := c.expr(e.Y)
		return c.binary(e.Op, left, right, e.Span)
	case *fe.AssignExpr:
		id, ok := e.X.(*fe.IdentExpr)
		if !ok {
			c.fail(e.Span, "assignment requires a local name")
			return bad
		}
		slot, ok := c.local(id.Name)
		if !ok {
			c.fail(e.Span, "undefined local %q", id.Name)
			return bad
		}
		var old qbeValue
		if e.Op != "=" {
			old = c.load(slot)
		}
		v := c.expr(e.Y)
		if slot.typ != v.typ {
			c.fail(e.Span, "assignment type %s does not match %s", v.typ, slot.typ)
			return bad
		}
		if e.Op != "=" {
			v = c.binary(strings.TrimSuffix(e.Op, "="), old, v, e.Span)
		}
		c.store(slot, v)
		return v
	case *fe.CallExpr:
		id, ok := e.Fn.(*fe.IdentExpr)
		if !ok {
			c.fail(e.Span, "native calls require a function name")
			return bad
		}
		if _, local := c.local(id.Name); local {
			c.fail(e.Span, "local %q is not callable", id.Name)
			return bad
		}
		sig, ok := c.program.Signatures[id.Name]
		if !ok {
			c.fail(e.Span, "undefined native function %q", id.Name)
			return bad
		}
		if len(e.Args) != len(sig.Params) {
			c.fail(e.Span, "%s takes %d arguments, got %d", id.Name, len(sig.Params), len(e.Args))
			return bad
		}
		var args []string
		for i, a := range e.Args {
			if a.Name != "" {
				c.fail(a.Span, "named arguments are unsupported")
			}
			v := c.expr(a.Value)
			if v.typ != sig.Params[i] {
				c.fail(a.Span, "argument type %s does not match %s", v.typ, sig.Params[i])
			}
			args = append(args, v.typ.qbe()+" "+v.ref)
		}
		return c.instruction(sig.Result, "call $%s(%s)", c.program.Symbols[id.Name], strings.Join(args, ", "))
	case *fe.TernaryExpr:
		yes, no, end := c.fresh("then"), c.fresh("else"), c.fresh("join")
		c.branch(c.expr(e.Cond), yes, no)
		c.label(yes)
		a := c.expr(e.Then)
		slot := c.slot(a.typ)
		c.store(slot, a)
		c.jump(end)
		c.label(no)
		b := c.expr(e.Else)
		if a.typ != b.typ {
			c.fail(e.Span, "branch types differ: %s and %s", a.typ, b.typ)
		}
		c.store(slot, b)
		c.jump(end)
		c.label(end)
		return c.load(slot)
	default:
		c.fail(e.Pos(), "unsupported expression %T", e)
	}
	return bad
}

func (c *qbeFunction) binary(op string, a, b qbeValue, sp fe.Span) qbeValue {
	bad := qbeValue{"0", Int64}
	if a.typ != b.typ {
		c.fail(sp, "operand types differ: %s and %s", a.typ, b.typ)
		return bad
	}
	if op == "==" || op == "!=" {
		cmp := "ceq"
		if op == "!=" {
			cmp = "cne"
		}
		return c.instruction(Bool, "%s%s %s, %s", cmp, a.typ.qbe(), a.ref, b.ref)
	}
	if a.typ != Int64 {
		c.fail(sp, "operator %q requires i64 operands", op)
		return bad
	}
	comparisons := map[string]string{"<": "csltl", "<=": "cslel", ">": "csgtl", ">=": "csgel"}
	if cmp, ok := comparisons[op]; ok {
		return c.instruction(Bool, "%s %s, %s", cmp, a.ref, b.ref)
	}
	ops := map[string]string{"+": "add", "-": "sub", "*": "mul", "/": "div", "%": "rem", "&": "and", "|": "or", "^": "xor", "<<": "shl", ">>": "sar"}
	if ins, ok := ops[op]; ok {
		// Division overflow/zero and out-of-range shifts trap rather than inheriting
		// target-specific undefined behavior. Overflow of add/sub/mul wraps.
		if op == "/" || op == "%" {
			zero := c.instruction(Bool, "ceql %s, 0", b.ref)
			min := c.instruction(Bool, "ceql %s, -9223372036854775808", a.ref)
			minusOne := c.instruction(Bool, "ceql %s, -1", b.ref)
			overflow := c.instruction(Bool, "and %s, %s", min.ref, minusOne.ref)
			invalid := c.instruction(Bool, "or %s, %s", zero.ref, overflow.ref)
			c.trapIf(invalid)
		}
		if op == "<<" || op == ">>" {
			c.trapIf(c.instruction(Bool, "cugtl %s, 63", b.ref))
		}
		return c.instruction(Int64, "%s %s, %s", ins, a.ref, b.ref)
	}
	c.fail(sp, "unsupported binary operator %q", op)
	return bad
}

func (c *qbeFunction) trapIf(v qbeValue) {
	trap, ok := c.fresh("trap"), c.fresh("valid")
	c.branch(v, trap, ok)
	c.label(trap)
	c.emit("hlt")
	c.terminated = true
	c.label(ok)
}
