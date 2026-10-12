package compiler

import (
	"fmt"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// EmitWASM emits a standalone WebAssembly 1.0 module for the typed scalar
// native subset. The selected function is exported as ingagi_entry; i64 uses
// WebAssembly i64 and bool uses a normalized i32. No imports, memory or WASI
// capabilities are implicit. Arithmetic traps match the QBE backend.
func EmitWASM(file *fe.File, entry string) ([]byte, error) {
	// Use the common native-subset gate before lowering. It verifies signatures,
	// scopes, operand types, returns and every supported control-flow construct.
	p, err := EmitQBE(file)
	if err != nil {
		return nil, err
	}
	if entry == "" {
		entry = "main"
	}
	indices := map[string]int{}
	for i, d := range file.Decls {
		indices[d.(*fe.FnDecl).Name] = i
	}
	index, ok := indices[entry]
	if !ok {
		return nil, fmt.Errorf("wasm: undefined entry %q", entry)
	}
	types, functions, bodies := wasmU(nil, uint64(len(file.Decls))), wasmU(nil, uint64(len(file.Decls))), wasmU(nil, uint64(len(file.Decls)))
	for i, d := range file.Decls {
		fn := d.(*fe.FnDecl)
		sig := p.Signatures[fn.Name]
		types = append(types, 0x60)
		types = wasmU(types, uint64(len(sig.Params)))
		for _, typ := range sig.Params {
			types = append(types, wasmType(typ))
		}
		types = append(types, 1, wasmType(sig.Result))
		functions = wasmU(functions, uint64(i))
		c := &wasmFunction{indices: indices, signatures: p.Signatures, scopes: []map[string]wasmLocal{{}}}
		for j, arg := range fn.Args {
			local := c.local(sig.Params[j])
			c.scopes[0][arg.Name] = local
			if local.typ == Bool {
				c.get(local)
				c.emit(0x45, 0x45)
				c.set(local, false)
			}
		}
		c.block(fn.Body)
		// QBE's return checker rejects fallthrough. An unreachable suffix also
		// gives WASM's structured validator a result on all syntactic paths.
		c.emit(0x00, 0x0b)
		if c.err != nil {
			return nil, c.err
		}
		locals := wasmU(nil, uint64(len(c.locals)-len(sig.Params)))
		for _, typ := range c.locals[len(sig.Params):] {
			locals = append(locals, 1, wasmType(typ))
		}
		body := append(locals, c.code...)
		bodies = append(wasmU(bodies, uint64(len(body))), body...)
	}
	exports := []byte{1}
	exports = wasmString(exports, "ingagi_entry")
	exports = append(exports, 0) // function export
	exports = wasmU(exports, uint64(index))
	out := []byte{'\x00', 'a', 's', 'm', 1, 0, 0, 0}
	for _, section := range []struct {
		id   byte
		data []byte
	}{{1, types}, {3, functions}, {7, exports}, {10, bodies}} {
		out = append(out, section.id)
		out = append(wasmU(out, uint64(len(section.data))), section.data...)
	}
	return out, nil
}

func wasmType(t ScalarType) byte {
	if t == Bool {
		return 0x7f
	}
	return 0x7e
}
func wasmU(out []byte, n uint64) []byte {
	for {
		b := byte(n & 127)
		n >>= 7
		if n != 0 {
			b |= 128
		}
		out = append(out, b)
		if n == 0 {
			return out
		}
	}
}
func wasmS(out []byte, n int64) []byte {
	for {
		b := byte(n & 127)
		n >>= 7
		done := n == 0 && b&64 == 0 || n == -1 && b&64 != 0
		if !done {
			b |= 128
		}
		out = append(out, b)
		if done {
			return out
		}
	}
}
func wasmString(out []byte, s string) []byte { return append(wasmU(out, uint64(len(s))), s...) }

type wasmLocal struct {
	index int
	typ   ScalarType
}
type wasmLoop struct{ exit, next int }
type wasmFunction struct {
	indices    map[string]int
	signatures map[string]Signature
	locals     []ScalarType
	scopes     []map[string]wasmLocal
	loops      []wasmLoop
	depth      int
	code       []byte
	err        error
}

func (c *wasmFunction) emit(b ...byte)         { c.code = append(c.code, b...) }
func (c *wasmFunction) operand(op byte, n int) { c.emit(op); c.code = wasmU(c.code, uint64(n)) }
func (c *wasmFunction) local(t ScalarType) wasmLocal {
	l := wasmLocal{len(c.locals), t}
	c.locals = append(c.locals, t)
	return l
}
func (c *wasmFunction) lookup(name string) wasmLocal {
	for i := len(c.scopes) - 1; i >= 0; i-- {
		if l, ok := c.scopes[i][name]; ok {
			return l
		}
	}
	if c.err == nil {
		c.err = fmt.Errorf("wasm: undefined local %q", name)
	}
	return wasmLocal{}
}
func (c *wasmFunction) get(l wasmLocal) { c.operand(0x20, l.index) }
func (c *wasmFunction) set(l wasmLocal, tee bool) {
	op := byte(0x21)
	if tee {
		op = 0x22
	}
	c.operand(op, l.index)
}
func (c *wasmFunction) constant(n int64) { c.emit(0x42); c.code = wasmS(c.code, n) }
func (c *wasmFunction) truth(t ScalarType) {
	if t == Int64 {
		c.emit(0x50, 0x45)
	} else {
		c.emit(0x45, 0x45)
	}
}
func (c *wasmFunction) block(b *fe.Block) bool {
	if b == nil {
		return false
	}
	c.scopes = append(c.scopes, map[string]wasmLocal{})
	defer func() { c.scopes = c.scopes[:len(c.scopes)-1] }()
	for _, s := range b.Stmts {
		if c.stmt(s) {
			return true
		}
	}
	return false
}
func (c *wasmFunction) stmt(s fe.Stmt) bool {
	switch s := s.(type) {
	case *fe.Block:
		return c.block(s)
	case *fe.VarDeclStmt:
		t := c.expr(s.Init)
		l := c.local(t)
		c.scopes[len(c.scopes)-1][s.Name] = l
		c.set(l, false)
	case *fe.ExprStmt:
		c.expr(s.X)
		c.emit(0x1a)
	case *fe.ReturnStmt:
		c.expr(s.Value)
		c.emit(0x0f)
		return true
	case *fe.IfStmt:
		c.truth(c.expr(s.Cond))
		c.emit(0x04, 0x40)
		c.depth++
		x := c.block(s.Then)
		c.emit(0x05)
		y := false
		if s.ElseIf != nil {
			y = c.stmt(s.ElseIf)
		} else {
			y = c.block(s.Else)
		}
		c.emit(0x0b)
		c.depth--
		return x && y
	case *fe.WhileStmt:
		c.loop(s.Body, s.Cond)
	case *fe.LoopStmt:
		c.loop(s.Body, nil)
	case *fe.BreakStmt:
		c.operand(0x0c, c.depth-c.loops[len(c.loops)-1].exit-1)
		return true
	case *fe.ContinueStmt:
		c.operand(0x0c, c.depth-c.loops[len(c.loops)-1].next-1)
		return true
	default:
		if c.err == nil {
			c.err = fmt.Errorf("%s: wasm: unsupported statement %T", s.Pos(), s)
		}
	}
	return false
}
func (c *wasmFunction) loop(body *fe.Block, cond fe.Expr) {
	exit := c.depth
	c.emit(0x02, 0x40, 0x03, 0x40)
	c.depth += 2
	c.loops = append(c.loops, wasmLoop{exit, exit + 1})
	if cond != nil {
		c.truth(c.expr(cond))
		c.emit(0x45)
		c.operand(0x0d, 1)
	}
	if !c.block(body) {
		c.operand(0x0c, 0)
	}
	c.loops = c.loops[:len(c.loops)-1]
	c.depth -= 2
	c.emit(0x0b, 0x0b)
}
func (c *wasmFunction) expr(e fe.Expr) ScalarType {
	switch e := e.(type) {
	case *fe.Lit:
		if e.Kind == fe.LitBool {
			c.emit(0x41)
			if e.Bool {
				c.emit(1)
			} else {
				c.emit(0)
			}
			return Bool
		}
		c.constant(e.Int)
		return Int64
	case *fe.IdentExpr:
		l := c.lookup(e.Name)
		c.get(l)
		return l.typ
	case *fe.UnaryExpr:
		if e.Op == "-" {
			c.constant(0)
			c.expr(e.X)
			c.emit(0x7d)
			return Int64
		}
		t := c.expr(e.X)
		switch e.Op {
		case "!":
			c.truth(t)
			c.emit(0x45)
			return Bool
		case "~":
			c.constant(-1)
			c.emit(0x85)
		}
		return t
	case *fe.BinaryExpr:
		if e.Op == "&&" || e.Op == "||" {
			c.truth(c.expr(e.X))
			c.emit(0x04, 0x7f)
			c.depth++
			if e.Op == "&&" {
				c.truth(c.expr(e.Y))
			} else {
				c.emit(0x41, 1)
			}
			c.emit(0x05)
			if e.Op == "||" {
				c.truth(c.expr(e.Y))
			} else {
				c.emit(0x41, 0)
			}
			c.emit(0x0b)
			c.depth--
			return Bool
		}
		t := c.expr(e.X)
		c.expr(e.Y)
		return c.binary(e.Op, t)
	case *fe.AssignExpr:
		l := c.lookup(e.X.(*fe.IdentExpr).Name)
		if e.Op != "=" {
			c.get(l)
		}
		c.expr(e.Y)
		if e.Op != "=" {
			c.binary(strings.TrimSuffix(e.Op, "="), l.typ)
		}
		c.set(l, true)
		return l.typ
	case *fe.CallExpr:
		name := ""
		switch fn := e.Fn.(type) {
		case *fe.IdentExpr:
			name = fn.Name
		case *fe.PathExpr:
			name = fn.Path.String()
		}
		for _, arg := range e.Args {
			c.expr(arg.Value)
		}
		c.operand(0x10, c.indices[name])
		return c.signatures[name].Result
	case *fe.TernaryExpr:
		c.truth(c.expr(e.Cond))
		c.emit(0x04, 0x40)
		pos := len(c.code) - 1
		c.depth++
		t := c.expr(e.Then)
		c.code[pos] = wasmType(t)
		c.emit(0x05)
		c.expr(e.Else)
		c.emit(0x0b)
		c.depth--
		return t
	default:
		if c.err == nil {
			c.err = fmt.Errorf("%s: wasm: unsupported expression %T", e.Pos(), e)
		}
	}
	return Int64
}
func (c *wasmFunction) binary(op string, t ScalarType) ScalarType {
	if op == "<<" || op == ">>" || op == "%" {
		b, a := c.local(Int64), c.local(Int64)
		c.set(b, false)
		c.set(a, false)
		if op == "%" {
			c.get(a)
			c.constant(-1 << 63)
			c.emit(0x51)
			c.get(b)
			c.constant(-1)
			c.emit(0x51, 0x71)
		} else {
			c.get(b)
			c.constant(63)
			c.emit(0x56)
		}
		c.emit(0x04, 0x40, 0x00, 0x0b)
		c.get(a)
		c.get(b)
	}
	if op == "==" || op == "!=" {
		ins := byte(0x51)
		if t == Bool {
			ins = 0x46
		}
		if op == "!=" {
			ins++
		}
		c.emit(ins)
		return Bool
	}
	if ins, ok := map[string]byte{"<": 0x53, ">": 0x55, "<=": 0x57, ">=": 0x59}[op]; ok {
		c.emit(ins)
		return Bool
	}
	ins, ok := map[string]byte{"+": 0x7c, "-": 0x7d, "*": 0x7e, "/": 0x7f, "%": 0x81, "&": 0x83, "|": 0x84, "^": 0x85, "<<": 0x86, ">>": 0x87}[op]
	if !ok {
		c.err = fmt.Errorf("wasm: unsupported operator %q", op)
	} else {
		c.emit(ins)
	}
	return Int64
}
