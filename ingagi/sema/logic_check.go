package sema

import fe "github.com/Chubek/JoltFX/ingagi/frontend"

// CheckLogic reports unreachable statements and fallthrough from functions
// with an explicit non-void return type. Conditions aren't constant-folded:
// control-flow coverage remains valid if an expression changes during reload.
func CheckLogic(f *fe.File, _ *Registry) []Diagnostic {
	v := &logicVisitor{}
	fe.Walk(v, f)
	return v.out
}

type logicVisitor struct {
	fe.BaseVisitor
	out []Diagnostic
}

func (v *logicVisitor) VisitDecl(d fe.Decl) bool {
	if fn, ok := d.(*fe.FnDecl); ok && fn.Body != nil {
		returns := v.block(fn.Body)
		if fn.Ret != nil && typeName(fn.Ret) != "void" && !returns {
			v.out = append(v.out, Diagnostic{Sev: Error, Code: "E0601", Msg: "function " + fn.Name + " may exit without returning a value", Span: fn.Span})
		}
	}
	if sys, ok := d.(*fe.SystemDecl); ok {
		for _, h := range sys.Hooks {
			v.block(h.Body)
		}
	}
	return true
}
func (v *logicVisitor) VisitExpr(e fe.Expr) bool {
	if fn, ok := e.(*fe.LambdaExpr); ok && fn.Body != nil {
		returns := v.block(fn.Body)
		if fn.Ret != nil && typeName(fn.Ret) != "void" && !returns {
			v.out = append(v.out, Diagnostic{Sev: Error, Code: "E0601", Msg: "closure may exit without returning a value", Span: fn.Span})
		}
	}
	return true
}
func (v *logicVisitor) block(b *fe.Block) bool {
	if b == nil {
		return false
	}
	terminated := false
	for _, st := range b.Stmts {
		if terminated {
			v.out = append(v.out, Diagnostic{Sev: Warning, Code: "W0602", Msg: "unreachable statement", Span: st.Pos()})
			break
		}
		terminated = v.stmt(st)
	}
	return terminated
}
func (v *logicVisitor) stmt(st fe.Stmt) bool {
	switch st := st.(type) {
	case *fe.ReturnStmt, *fe.BreakStmt, *fe.ContinueStmt:
		return true
	case *fe.Block:
		return v.block(st)
	case *fe.IfStmt:
		x, y := v.block(st.Then), v.block(st.Else)
		if st.ElseIf != nil {
			y = v.stmt(st.ElseIf)
		}
		return x && y
	case *fe.MatchStmt:
		all, exhaustive := len(st.Arms) > 0, false
		for _, a := range st.Arms {
			end := v.block(a.Body)
			all = all && end
			if a.Guard == nil {
				switch a.Pat.(type) {
				case *fe.WildcardPat, *fe.BindPat:
					exhaustive = true
				}
			}
		}
		return all && exhaustive
	case *fe.WhileStmt:
		v.block(st.Body)
	case *fe.ForStmt:
		v.block(st.Body)
	case *fe.ForEachStmt:
		v.block(st.Body)
	case *fe.LoopStmt:
		v.block(st.Body)
	case *fe.UnsafeStmt:
		return v.block(st.Body)
	case *fe.DeferStmt:
		v.block(st.Body)
	case *fe.OnStmt:
		v.block(st.Body)
	}
	return false
}
