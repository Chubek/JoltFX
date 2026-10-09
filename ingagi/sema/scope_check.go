package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Scope analysis: duplicate declarations, undefined names, and arity.
// The parser guarantees shape; this pass guarantees linkage.

// CheckScope validates module-wide and per-function scoping.
func CheckScope(f *fe.File, r *Registry) []Diagnostic {
	var out []Diagnostic
	seen := map[string]fe.Span{}
	dup := func(kind, name string, sp fe.Span) {
		if prev, ok := seen[kind+"\x00"+name]; ok {
			_ = prev
			out = append(out, Diagnostic{Sev: Error, Code: "E0101", Msg: "duplicate " + kind + " " + name, Span: sp})
			return
		}
		seen[kind+"\x00"+name] = sp
	}
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.FnDecl:
			dup("function", d.Name, d.Span)
			checkFnScope(d, r, &out)
		case *fe.StructDecl:
			dup("struct", d.Name, d.Span)
			for _, m := range d.Methods {
				checkFnScope(m, r, &out)
			}
		case *fe.ComponentDecl:
			dup("component", d.Name, d.Span)
			for _, m := range d.Methods {
				checkFnScope(m, r, &out)
			}
		case *fe.EnumDecl:
			dup("enum", d.Name, d.Span)
		case *fe.EntityDecl:
			dup("entity", d.Name, d.Span)
		case *fe.SystemDecl:
			dup("system", d.Name, d.Span)
			for _, fn := range d.Fns {
				checkFnScope(fn, r, &out)
			}
			for i := range d.Hooks {
				checkHookScope(&d.Hooks[i], r, &out)
			}
		case *fe.EventDecl:
			dup("event", d.Name, d.Span)
		case *fe.ConstDecl:
			dup("const", d.Name, d.Span)
		case *fe.ResourceDecl:
			dup("resource", d.Name, d.Span)
		case *fe.ShaderDecl:
			dup("shader", d.Name, d.Span)
		case *fe.PipelineDecl:
			dup("pipeline", d.Name, d.Span)
		}
	}
	return out
}

// scope tracks bound names in one function.
type scope struct {
	bound map[string]bool
}

func newScope(params []fe.Param) *scope {
	s := &scope{bound: map[string]bool{}}
	for _, p := range params {
		s.bound[p.Name] = true
	}
	return s
}

// checkFnScope validates names inside a function body.
func checkFnScope(d *fe.FnDecl, r *Registry, out *[]Diagnostic) {
	s := newScope(d.Args)
	if d.Body != nil {
		checkBlockScope(d.Body, s, r, out)
	}
}

func checkHookScope(h *fe.SystemHook, r *Registry, out *[]Diagnostic) {
	s := newScope(h.Params)
	checkBlockScope(h.Body, s, r, out)
}

func checkBlockScope(b *fe.Block, s *scope, r *Registry, out *[]Diagnostic) {
	if b == nil {
		return
	}
	for _, st := range b.Stmts {
		checkStmtScope(st, s, r, out)
	}
}

func checkStmtScope(st fe.Stmt, s *scope, r *Registry, out *[]Diagnostic) {
	switch st := st.(type) {
	case *fe.Block:
		inner := &scope{bound: map[string]bool{}}
		for k, v := range s.bound {
			inner.bound[k] = v
		}
		checkBlockScope(st, inner, r, out)
	case *fe.VarDeclStmt:
		if st.Init != nil {
			checkExprScope(st.Init, s, r, out)
		}
		s.bound[st.Name] = true
	case *fe.ExprStmt:
		checkExprScope(st.X, s, r, out)
	case *fe.ReturnStmt:
		if st.Value != nil {
			checkExprScope(st.Value, s, r, out)
		}
	case *fe.IfStmt:
		checkExprScope(st.Cond, s, r, out)
		checkBlockScope(st.Then, s, r, out)
		checkBlockScope(st.Else, s, r, out)
		if st.ElseIf != nil {
			checkStmtScope(st.ElseIf, s, r, out)
		}
	case *fe.WhileStmt:
		checkExprScope(st.Cond, s, r, out)
		checkBlockScope(st.Body, s, r, out)
	case *fe.ForStmt:
		checkExprScope(st.Iter, s, r, out)
		bindPattern(st.Pat, s)
		checkBlockScope(st.Body, s, r, out)
	case *fe.LoopStmt:
		checkBlockScope(st.Body, s, r, out)
	case *fe.MatchStmt:
		checkExprScope(st.X, s, r, out)
		for _, a := range st.Arms {
			bindPattern(a.Pat, s)
			if a.Guard != nil {
				checkExprScope(a.Guard, s, r, out)
			}
			if a.Value != nil {
				checkExprScope(a.Value, s, r, out)
			}
			checkBlockScope(a.Body, s, r, out)
		}
	case *fe.ForEachStmt:
		checkExprScope(st.Iter, s, r, out)
		for _, b := range st.Bindings {
			name := b.Alias
			if name == "" {
				name = b.Name
			}
			s.bound[name] = true
		}
		checkBlockScope(st.Body, s, r, out)
	case *fe.EmitStmt:
		checkExprScope(st.Event, s, r, out)
	case *fe.OnStmt:
		checkBlockScope(st.Body, s, r, out)
	case *fe.BindStmt:
		checkExprScope(st.Iter, s, r, out)
	case *fe.DeclStmt:
		if fn, ok := st.Decl.(*fe.FnDecl); ok {
			checkFnScope(fn, r, out)
		}
	}
}

// bindPattern introduces pattern-bound names.
func bindPattern(p fe.Pattern, s *scope) {
	switch p := p.(type) {
	case *fe.BindPat:
		s.bound[p.Name] = true
	case *fe.TuplePat:
		for _, e := range p.Elems {
			bindPattern(e, s)
		}
	case *fe.StructPat:
		for _, f := range p.Fields {
			if f.Pat != nil {
				bindPattern(f.Pat, s)
			} else {
				s.bound[f.Field] = true
			}
		}
	case *fe.ArrayPat:
		for _, e := range p.Elems {
			bindPattern(e, s)
		}
		if p.HasRest && p.Rest != "" {
			s.bound[p.Rest] = true
		}
	case *fe.OrPat:
		for _, e := range p.Alts {
			bindPattern(e, s)
		}
	}
}

// checkExprScope flags calls to undeclared functions and loads of
// undeclared names (locals, globals, functions and builtins all count).
func checkExprScope(e fe.Expr, s *scope, r *Registry, out *[]Diagnostic) {
	switch e := e.(type) {
	case *fe.IdentExpr:
		if !s.bound[e.Name] && !globalKnown(e.Name, r) {
			*out = append(*out, Diagnostic{Sev: Error, Code: "E0102", Msg: "undefined name " + e.Name, Span: e.Span})
		}
	case *fe.PathExpr:
		head := e.Path.Parts[0].Name
		if len(e.Path.Parts) == 1 && !s.bound[head] && !globalKnown(head, r) {
			*out = append(*out, Diagnostic{Sev: Error, Code: "E0102", Msg: "undefined name " + head, Span: e.Span})
		}
	case *fe.CallExpr:
		switch fn := e.Fn.(type) {
		case *fe.IdentExpr:
			if !s.bound[fn.Name] {
				if sig, ok := r.Funcs[fn.Name]; ok {
					if len(e.Args) > sig.Params {
						*out = append(*out, Diagnostic{Sev: Error, Code: "E0103", Msg: "too many arguments to " + fn.Name, Span: e.Span})
					}
				} else if !globalKnown(fn.Name, r) && !isBuiltin(fn.Name) {
					*out = append(*out, Diagnostic{Sev: Error, Code: "E0102", Msg: "undefined function " + fn.Name, Span: e.Span})
				}
			}
		case *fe.PathExpr:
			// Qualified calls resolve at link time (methods, enum ctors).
		default:
			checkExprScope(e.Fn, s, r, out)
		}
		for _, a := range e.Args {
			checkExprScope(a.Value, s, r, out)
		}
	case *fe.UnaryExpr:
		checkExprScope(e.X, s, r, out)
	case *fe.BinaryExpr:
		checkExprScope(e.X, s, r, out)
		checkExprScope(e.Y, s, r, out)
	case *fe.AssignExpr:
		checkExprScope(e.X, s, r, out)
		checkExprScope(e.Y, s, r, out)
	case *fe.TernaryExpr:
		checkExprScope(e.Cond, s, r, out)
		checkExprScope(e.Then, s, r, out)
		checkExprScope(e.Else, s, r, out)
	case *fe.MemberExpr:
		checkExprScope(e.X, s, r, out)
	case *fe.IndexExpr:
		checkExprScope(e.X, s, r, out)
		checkExprScope(e.Index, s, r, out)
	case *fe.QuestionExpr:
		checkExprScope(e.X, s, r, out)
	case *fe.TupleExpr:
		for _, x := range e.Elems {
			checkExprScope(x, s, r, out)
		}
	case *fe.ArrayExpr:
		for _, x := range e.Elems {
			checkExprScope(x, s, r, out)
		}
	case *fe.ArrayRepeatExpr:
		checkExprScope(e.Elem, s, r, out)
		checkExprScope(e.Count, s, r, out)
	case *fe.StructExpr:
		for _, f := range e.Fields {
			if f.Value != nil {
				checkExprScope(f.Value, s, r, out)
			}
		}
	case *fe.LambdaExpr:
		inner := &scope{bound: map[string]bool{}}
		for k, v := range s.bound {
			inner.bound[k] = v
		}
		for _, a := range e.Params {
			inner.bound[a.Name] = true
		}
		if e.Single != nil {
			checkExprScope(e.Single, inner, r, out)
		}
		checkBlockScope(e.Body, inner, r, out)
	case *fe.IfExpr:
		checkExprScope(e.Cond, s, r, out)
		checkBlockScope(e.Then, s, r, out)
		checkBlockScope(e.Else, s, r, out)
		if e.ElseIf != nil {
			checkExprScope(e.ElseIf, s, r, out)
		}
	case *fe.MatchExpr:
		checkExprScope(e.X, s, r, out)
		for _, a := range e.Arms {
			bindPattern(a.Pat, s)
			if a.Value != nil {
				checkExprScope(a.Value, s, r, out)
			}
			checkBlockScope(a.Body, s, r, out)
		}
	}
}

// globalKnown reports module-level names (types, consts, resources…).
func globalKnown(name string, r *Registry) bool {
	if _, ok := r.Types[name]; ok {
		return true
	}
	if _, ok := r.Funcs[name]; ok {
		return true
	}
	if _, ok := r.Consts[name]; ok {
		return true
	}
	if _, ok := r.Resources[name]; ok {
		return true
	}
	if _, ok := r.Entities[name]; ok {
		return true
	}
	if _, ok := r.Events[name]; ok {
		return true
	}
	if name == "world" || name == "self" {
		return true
	}
	return false
}

// isBuiltin reports std builtins callable without import.
func isBuiltin(name string) bool {
	switch name {
	case "print", "len", "type", "keys", "push", "pop", "range", "str",
		"int", "float", "assert", "sin", "cos", "tan", "sqrt", "exp",
		"log", "floor", "ceil", "abs", "min", "max", "pow",
		"vec2", "vec3", "vec4", "quat", "mat4_identity", "mat4_translate",
		"mat4_scale", "mat4_rotate", "mat4_perspective", "mat4_lookat",
		"dot", "cross", "normalize", "lerp", "spawn", "despawn",
		"add_component", "remove_component", "has_component",
		"get_component", "set_resource", "get_resource",
		"set_parent", "get_parent":
		return true
	}
	return false
}
