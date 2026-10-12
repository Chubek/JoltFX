package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// CheckSecurity reports constructs that reach outside the language's own
// value model: raw pointer types and pointer operators, native `extern`
// declarations, and `unsafe` blocks. Ingagi has no sandbox, so this pass
// exists to make each of those boundaries explicit in review rather than to
// claim that scripts are contained. Values arriving from host functions
// (taint source) are handled by CheckTaint.
//
// Severity: `extern` declarations and pointer types/operators are errors
// unless they appear inside an `unsafe` block, where they are reported as
// notes. Nothing here is silently dropped.
func CheckSecurity(f *fe.File, _ *Registry) []Diagnostic {
	v := &securityVisitor{}
	v.file(f)
	return v.out
}

// securitySinks names the forms that escape the value model.
var pointerOps = map[string]bool{"*": true, "&": true}

type securityVisitor struct {
	out    []Diagnostic
	unsafe int // nesting depth of enclosing `unsafe` blocks
}

func (v *securityVisitor) error(code, msg string, sp fe.Span) {
	v.out = append(v.out, Diagnostic{Sev: Error, Code: code, Msg: msg, Span: sp})
}

func (v *securityVisitor) warn(code, msg string, sp fe.Span) {
	v.out = append(v.out, Diagnostic{Sev: Warning, Code: code, Msg: msg, Span: sp})
}

// escape reports a construct that crosses the value-model boundary. Inside an
// `unsafe` block it is demoted to a note so the block still marks intent.
func (v *securityVisitor) escape(sp fe.Span, what string) {
	if v.unsafe > 0 {
		v.out = append(v.out, Diagnostic{Sev: Note, Code: "I0901",
			Msg: what + " inside unsafe block", Span: sp})
		return
	}
	v.error("E0901", what+" requires an unsafe block", sp)
}

func (v *securityVisitor) file(f *fe.File) {
	for _, imp := range f.Imports {
		_ = imp // imports are checked by CheckImport; nothing to gate here
	}
	for _, d := range f.Decls {
		v.decl(d)
	}
}

func (v *securityVisitor) decl(d fe.Decl) {
	switch d := d.(type) {
	case *fe.ExternDecl:
		// An extern declaration binds a foreign library into script scope.
		// It is the strongest escape hatch in the language, so it is always
		// an error outside `unsafe` (extern bodies cannot be inside `unsafe`
		// blocks anyway).
		v.error("E0902", "extern declaration \""+d.Lib+"\" is not allowed in sandboxed code", d.Span)
	case *fe.FnDecl:
		v.fn(d)
	case *fe.StructDecl:
		for _, f := range d.Fields {
			v.typ(f.Type)
		}
		for _, m := range d.Methods {
			v.fn(m)
		}
	case *fe.ComponentDecl:
		v.typ(d.Base)
		for _, f := range d.Fields {
			v.typ(f.Type)
		}
		for _, m := range d.Methods {
			v.fn(m)
		}
	case *fe.EventDecl:
		v.typ(d.Base)
		for _, f := range d.Fields {
			v.typ(f.Type)
		}
	case *fe.EnumDecl:
		v.typ(d.Repr)
		for _, e := range d.Variants {
			for _, t := range e.Types {
				v.typ(t)
			}
		}
	case *fe.TypeAliasDecl:
		v.typ(d.Target)
	case *fe.EntityDecl:
		for _, p := range d.Props {
			v.typ(p.Type)
		}
	case *fe.SystemDecl:
		for _, fn := range d.Fns {
			v.fn(fn)
		}
		for _, h := range d.Hooks {
			for _, p := range h.Params {
				v.typ(p.Type)
			}
			v.block(h.Body)
		}
	case *fe.InterfaceDecl:
		for _, m := range d.Methods {
			v.fn(m)
		}
	case *fe.ExtendDecl:
		v.typ(d.Target)
		v.block(d.Body)
	}
}

func (v *securityVisitor) fn(d *fe.FnDecl) {
	for _, a := range d.Args {
		v.typ(a.Type)
	}
	v.typ(d.Ret)
	v.block(d.Body)
}

func (v *securityVisitor) typ(t fe.Type) {
	switch t := t.(type) {
	case *fe.PtrType:
		v.escape(t.Span, "raw pointer type")
		v.typ(t.Elem)
	case *fe.RefType:
		v.typ(t.Elem)
	case *fe.TypeName:
		for _, a := range t.Args {
			v.typ(a)
		}
	case *fe.ArrayType:
		v.typ(t.Elem)
	case *fe.TupleType:
		for _, e := range t.Elems {
			v.typ(e)
		}
	case *fe.FnType:
		for _, p := range t.Params {
			v.typ(p)
		}
		v.typ(t.Ret)
	}
}

func (v *securityVisitor) block(b *fe.Block) {
	if b == nil {
		return
	}
	for _, st := range b.Stmts {
		v.stmt(st)
	}
}

func (v *securityVisitor) stmt(st fe.Stmt) {
	switch st := st.(type) {
	case *fe.Block:
		v.block(st)
	case *fe.VarDeclStmt:
		v.typ(st.Type)
		v.expr(st.Init)
	case *fe.ExprStmt:
		v.expr(st.X)
	case *fe.ReturnStmt:
		v.expr(st.Value)
	case *fe.IfStmt:
		v.expr(st.Cond)
		v.block(st.Then)
		v.block(st.Else)
		if st.ElseIf != nil {
			v.stmt(st.ElseIf)
		}
	case *fe.WhileStmt:
		v.expr(st.Cond)
		v.block(st.Body)
	case *fe.ForStmt:
		v.expr(st.Iter)
		v.block(st.Body)
	case *fe.LoopStmt:
		v.block(st.Body)
	case *fe.MatchStmt:
		v.expr(st.X)
		for _, a := range st.Arms {
			v.expr(a.Guard)
			v.expr(a.Value)
			v.block(a.Body)
		}
	case *fe.DeferStmt:
		v.block(st.Body)
	case *fe.UnsafeStmt:
		v.unsafe++
		v.block(st.Body)
		v.unsafe--
	case *fe.ForEachStmt:
		v.expr(st.Iter)
		v.block(st.Body)
	case *fe.BindStmt:
		v.expr(st.Iter)
	case *fe.EmitStmt:
		v.expr(st.Event)
	case *fe.OnStmt:
		v.block(st.Body)
	case *fe.BreakStmt:
		v.expr(st.Value)
	case *fe.DeclStmt:
		v.decl(st.Decl)
	}
}

func (v *securityVisitor) expr(e fe.Expr) {
	if e == nil {
		return
	}
	switch e := e.(type) {
	case *fe.UnaryExpr:
		if pointerOps[e.Op] {
			v.escape(e.Span, "pointer operator "+e.Op)
		}
		v.expr(e.X)
	case *fe.BinaryExpr:
		v.expr(e.X)
		v.expr(e.Y)
	case *fe.AssignExpr:
		v.expr(e.X)
		v.expr(e.Y)
	case *fe.TernaryExpr:
		v.expr(e.Cond)
		v.expr(e.Then)
		v.expr(e.Else)
	case *fe.CallExpr:
		v.expr(e.Fn)
		for _, a := range e.Args {
			v.expr(a.Value)
		}
	case *fe.MemberExpr:
		v.expr(e.X)
	case *fe.IndexExpr:
		v.expr(e.X)
		v.expr(e.Index)
	case *fe.QuestionExpr:
		v.expr(e.X)
	case *fe.TupleExpr:
		for _, x := range e.Elems {
			v.expr(x)
		}
	case *fe.ArrayExpr:
		for _, x := range e.Elems {
			v.expr(x)
		}
	case *fe.ArrayRepeatExpr:
		v.expr(e.Elem)
		v.expr(e.Count)
	case *fe.StructExpr:
		for _, f := range e.Fields {
			v.expr(f.Value)
		}
	case *fe.LambdaExpr:
		for _, p := range e.Params {
			v.typ(p.Type)
		}
		v.typ(e.Ret)
		v.expr(e.Single)
		v.block(e.Body)
	case *fe.IfExpr:
		v.expr(e.Cond)
		v.block(e.Then)
		v.block(e.Else)
		v.expr(e.ElseIf)
	case *fe.MatchExpr:
		v.expr(e.X)
		for _, a := range e.Arms {
			v.expr(a.Guard)
			v.expr(a.Value)
			v.block(a.Body)
		}
	}
}