package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"strings"
)

// CheckType checks statically known types without guessing the types of dynamic
// host calls. Unannotated/dynamic values remain unknown; this is gradual checking,
// not an assertion that all VM operations are statically safe.
func CheckType(f *fe.File, r *Registry) []Diagnostic {
	c := &typeChecker{r: r, funcs: map[string]*fe.FnDecl{}, globals: map[string]string{}}
	for _, d := range f.Decls {
		if fn, ok := d.(*fe.FnDecl); ok {
			c.funcs[fn.Name] = fn
		}
	}
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.ConstDecl:
			got := c.expr(d.Value, c.globals)
			want := canonical(typeName(d.Type))
			c.expect(want, got, d.Span)
			if want == "?" {
				want = got
			}
			c.globals[d.Name] = want
		case *fe.ResourceDecl:
			c.expect(canonical(typeName(d.Type)), c.expr(d.Value, c.globals), d.Span)
		}
	}
	for _, d := range f.Decls {
		c.decl(d, c.globals)
	}
	return c.out
}

type typeChecker struct {
	r       *Registry
	funcs   map[string]*fe.FnDecl
	globals map[string]string
	out     []Diagnostic
	ret     string
}

func canonical(t string) string {
	if strings.HasSuffix(t, "?") && t != "?" {
		return canonical(strings.TrimSuffix(t, "?")) + "?"
	}
	switch t {
	case "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "int", "uint", "char":
		return "int"
	case "f32", "f64", "float", "double":
		return "float"
	case "str", "String":
		return "string"
	}
	if strings.HasPrefix(t, "[") && strings.HasSuffix(t, "]") {
		return "[" + canonical(t[1:len(t)-1]) + "]"
	}
	return t
}

func typeScope(s map[string]string) map[string]string {
	r := make(map[string]string, len(s))
	for k, v := range s {
		r[k] = v
	}
	return r
}
func (c *typeChecker) error(code, msg string, sp fe.Span) {
	c.out = append(c.out, Diagnostic{Sev: Error, Code: code, Msg: msg, Span: sp})
}
func (c *typeChecker) expect(want, got string, sp fe.Span) {
	if want == got {
		return
	}
	if strings.HasSuffix(want, "?") && want != "?" {
		if got == "nil" {
			return
		}
		want = strings.TrimSuffix(want, "?")
	}
	if want == "void" && got == "nil" {
		return
	}
	if want != "?" && got != "?" && want != got && !(want == "float" && got == "int") {
		c.error("E0201", "expected "+want+", got "+got, sp)
	}
}
func (c *typeChecker) fn(fn *fe.FnDecl, s map[string]string) {
	inner := typeScope(s)
	for _, a := range fn.Args {
		t := canonical(typeName(a.Type))
		c.expect(t, c.expr(a.Default, inner), a.Span)
		inner[a.Name] = t
	}
	old := c.ret
	c.ret = canonical(typeName(fn.Ret))
	c.block(fn.Body, inner)
	c.ret = old
}
func (c *typeChecker) decl(d fe.Decl, s map[string]string) {
	switch d := d.(type) {
	case *fe.FnDecl:
		c.fn(d, s)
	case *fe.StructDecl:
		for _, fn := range d.Methods {
			c.fn(fn, s)
		}
	case *fe.ComponentDecl:
		for _, f := range d.Fields {
			c.expect(canonical(typeName(f.Type)), c.expr(f.Default, s), f.Span)
		}
		for _, fn := range d.Methods {
			c.fn(fn, s)
		}
	case *fe.EventDecl:
		for _, f := range d.Fields {
			c.expect(canonical(typeName(f.Type)), c.expr(f.Default, s), f.Span)
		}
	case *fe.SystemDecl:
		for _, fn := range d.Fns {
			c.fn(fn, s)
		}
		for _, h := range d.Hooks {
			inner := typeScope(s)
			for _, a := range h.Params {
				inner[a.Name] = canonical(typeName(a.Type))
			}
			old := c.ret
			c.ret = "?"
			c.block(h.Body, inner)
			c.ret = old
		}
	case *fe.ExternDecl:
		for _, fn := range d.Funcs {
			c.fn(fn, s)
		}
	case *fe.ExtendDecl:
		c.block(d.Body, s)
	}
}
func (c *typeChecker) block(b *fe.Block, s map[string]string) {
	if b == nil {
		return
	}
	inner := typeScope(s)
	for _, st := range b.Stmts {
		c.stmt(st, inner)
	}
}
func (c *typeChecker) stmt(st fe.Stmt, s map[string]string) {
	switch st := st.(type) {
	case *fe.Block:
		c.block(st, s)
	case *fe.VarDeclStmt:
		got := c.expr(st.Init, s)
		want := canonical(typeName(st.Type))
		c.expect(want, got, st.Span)
		if want == "?" {
			want = got
		}
		s[st.Name] = want
	case *fe.ExprStmt:
		c.expr(st.X, s)
	case *fe.ReturnStmt:
		got := "nil"
		if st.Value != nil {
			got = c.expr(st.Value, s)
		}
		c.expect(c.ret, got, st.Span)
	case *fe.IfStmt:
		c.expr(st.Cond, s)
		c.block(st.Then, s)
		c.block(st.Else, s)
		if st.ElseIf != nil {
			c.stmt(st.ElseIf, typeScope(s))
		}
	case *fe.WhileStmt:
		c.expr(st.Cond, s)
		c.block(st.Body, s)
	case *fe.LoopStmt:
		c.block(st.Body, s)
	case *fe.ForStmt:
		t := c.expr(st.Iter, s)
		inner := typeScope(s)
		elem := "?"
		if strings.HasPrefix(t, "[") {
			elem = strings.TrimSuffix(t[1:], "]")
		}
		bindTypes(st.Pat, elem, inner)
		c.block(st.Body, inner)
	case *fe.ForEachStmt:
		c.expr(st.Iter, s)
		inner := typeScope(s)
		for _, b := range st.Bindings {
			n := b.Alias
			if n == "" {
				n = b.Name
			}
			inner[n] = b.Name
		}
		c.block(st.Body, inner)
	case *fe.MatchStmt:
		c.expr(st.X, s)
		c.arms(st.Arms, s)
	case *fe.DeferStmt:
		c.block(st.Body, s)
	case *fe.UnsafeStmt:
		c.block(st.Body, s)
	case *fe.EmitStmt:
		c.expr(st.Event, s)
	case *fe.BindStmt:
		c.expr(st.Iter, s)
	case *fe.OnStmt:
		c.block(st.Body, s)
	case *fe.DeclStmt:
		c.decl(st.Decl, s)
	case *fe.BreakStmt:
		c.expr(st.Value, s)
	}
}
func bindTypes(p fe.Pattern, t string, s map[string]string) {
	b := newScope(nil)
	bindPattern(p, b)
	for n := range b.bound {
		s[n] = "?"
	}
	if p, ok := p.(*fe.BindPat); ok {
		s[p.Name] = t
	}
}
func (c *typeChecker) arms(arms []fe.MatchArm, s map[string]string) string {
	result := "?"
	for _, a := range arms {
		inner := typeScope(s)
		bindTypes(a.Pat, "?", inner)
		c.expr(a.Guard, inner)
		if a.Value != nil {
			t := c.expr(a.Value, inner)
			if result == "?" {
				result = t
			} else {
				c.expect(result, t, a.Span)
			}
		}
		c.block(a.Body, inner)
	}
	return result
}
func (c *typeChecker) expr(e fe.Expr, s map[string]string) string {
	if e == nil {
		return "?"
	}
	switch e := e.(type) {
	case *fe.Lit:
		switch e.Kind {
		case fe.LitInt, fe.LitChar:
			return "int"
		case fe.LitFloat:
			return "float"
		case fe.LitString:
			return "string"
		case fe.LitBool:
			return "bool"
		case fe.LitNull:
			return "nil"
		}
	case *fe.IdentExpr:
		if t, ok := s[e.Name]; ok {
			return t
		}
		return "?"
	case *fe.PathExpr:
		if t, ok := c.globals[e.Path.String()]; ok {
			return t
		}
		return "?"
	case *fe.SelfExpr:
		if t, ok := s["self"]; ok {
			return t
		}
		return "?"
	case *fe.UnaryExpr:
		t := c.expr(e.X, s)
		if e.Op == "!" {
			return "bool"
		}
		if (e.Op == "-" || e.Op == "+" || e.Op == "~") && t != "?" && t != "int" && t != "float" {
			c.error("E0202", "invalid operand for "+e.Op+": "+t, e.Span)
		}
		if e.Op == "~" {
			c.expect("int", t, e.Span)
		}
		return t
	case *fe.BinaryExpr:
		x, y := c.expr(e.X, s), c.expr(e.Y, s)
		switch e.Op {
		case "==", "!=", "<", "<=", ">", ">=":
			return "bool"
		case "&&", "||":
			if x == "bool" && y == "bool" {
				return "bool"
			}
			return "?"
		}
		if x == "?" || y == "?" {
			return "?"
		}
		if e.Op == "+" && x == "string" && y == "string" {
			return "string"
		}
		if e.Op == ".." || e.Op == "..=" {
			c.expect("int", x, e.X.Pos())
			c.expect("int", y, e.Y.Pos())
			return "[int]"
		}
		if (x != "int" && x != "float") || (y != "int" && y != "float") {
			c.error("E0202", "invalid operands for "+e.Op+": "+x+", "+y, e.Span)
			return "?"
		}
		switch e.Op {
		case "&", "|", "^", "<<", ">>":
			c.expect("int", x, e.X.Pos())
			c.expect("int", y, e.Y.Pos())
			return "int"
		}
		if x == "float" || y == "float" {
			return "float"
		}
		return "int"
	case *fe.AssignExpr:
		x, y := c.expr(e.X, s), c.expr(e.Y, s)
		c.expect(x, y, e.Span)
		return y
	case *fe.TernaryExpr:
		c.expr(e.Cond, s)
		x, y := c.expr(e.Then, s), c.expr(e.Else, s)
		c.expect(x, y, e.Span)
		return x
	case *fe.CallExpr:
		name := ""
		switch fn := e.Fn.(type) {
		case *fe.IdentExpr:
			name = fn.Name
		case *fe.PathExpr:
			name = fn.Path.String()
		default:
			c.expr(e.Fn, s)
		}
		fn := c.funcs[name]
		if _, shadow := s[name]; shadow {
			fn = nil
		}
		for i, a := range e.Args {
			t := c.expr(a.Value, s)
			if fn != nil && i < len(fn.Args) {
				c.expect(canonical(typeName(fn.Args[i].Type)), t, a.Span)
			}
		}
		if fn != nil {
			return canonical(typeName(fn.Ret))
		}
		if _, shadow := s[name]; shadow {
			return "?" // A local callable is not the same-named builtin.
		}
		switch name {
		case "len", "int":
			return "int"
		case "str", "type":
			return "string"
		case "float", "noise2", "noise3", "fbm3":
			return "float"
		}
		return "?"
	case *fe.MemberExpr:
		t := c.expr(e.X, s)
		if td := c.r.Types[t]; td != nil {
			if ft, ok := td.Fields[e.Field]; ok {
				return canonical(ft)
			}
			if c.r.Funcs[t+"::"+e.Field] == nil {
				c.error("E0203", "unknown field "+t+"."+e.Field, e.Span)
			}
		}
		return "?"
	case *fe.IndexExpr:
		t := c.expr(e.X, s)
		c.expect("int", c.expr(e.Index, s), e.Index.Pos())
		if strings.HasPrefix(t, "[") {
			return strings.TrimSuffix(t[1:], "]")
		}
		return "?"
	case *fe.QuestionExpr:
		t := c.expr(e.X, s)
		if t == "?" {
			return t
		}
		return strings.TrimSuffix(t, "?")
	case *fe.ArrayExpr:
		t := "?"
		for _, x := range e.Elems {
			next := c.expr(x, s)
			if t == "?" {
				t = next
			} else {
				c.expect(t, next, x.Pos())
			}
		}
		return "[" + t + "]"
	case *fe.ArrayRepeatExpr:
		t := c.expr(e.Elem, s)
		c.expect("int", c.expr(e.Count, s), e.Count.Pos())
		return "[" + t + "]"
	case *fe.TupleExpr:
		for _, x := range e.Elems {
			c.expr(x, s)
		}
		return "tuple"
	case *fe.StructExpr:
		t := e.Type.String()
		td := c.r.Types[t]
		for _, f := range e.Fields {
			value := f.Value
			if value == nil {
				value = &fe.IdentExpr{Name: f.Field, Span: f.Span}
			}
			got := c.expr(value, s)
			if td != nil {
				if want, ok := td.Fields[f.Field]; ok {
					c.expect(canonical(want), got, f.Span)
				} else {
					c.error("E0203", "unknown field "+t+"."+f.Field, f.Span)
				}
			}
		}
		return t
	case *fe.LambdaExpr:
		inner := typeScope(s)
		for _, a := range e.Params {
			inner[a.Name] = canonical(typeName(a.Type))
		}
		old := c.ret
		c.ret = canonical(typeName(e.Ret))
		c.expect(c.ret, c.expr(e.Single, inner), e.Span)
		c.block(e.Body, inner)
		c.ret = old
		return "fn"
	case *fe.IfExpr:
		c.expr(e.Cond, s)
		c.block(e.Then, s)
		c.block(e.Else, s)
		c.expr(e.ElseIf, s)
		return "?"
	case *fe.MatchExpr:
		c.expr(e.X, s)
		return c.arms(e.Arms, s)
	}
	return "?"
}
