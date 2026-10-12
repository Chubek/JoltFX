package sema

import fe "github.com/Chubek/JoltFX/ingagi/frontend"

// CheckSyntax checks contextual AST constraints not encoded in the grammar.
func CheckSyntax(f *fe.File, _ *Registry) []Diagnostic {
	v := &syntaxVisitor{}
	var names []string
	for _, d := range f.Decls {
		switch d.(type) {
		case *fe.ExternDecl, *fe.ExtendDecl:
			continue
		}
		names = append(names, d.DeclName())
	}
	v.duplicate(names, f.Span, "module symbol")
	fe.Walk(v, f)
	return v.out
}

type syntaxVisitor struct {
	fe.BaseVisitor
	out []Diagnostic
}

func (v *syntaxVisitor) duplicate(names []string, sp fe.Span, kind string) {
	seen := map[string]bool{}
	for _, n := range names {
		if seen[n] {
			v.out = append(v.out, Diagnostic{Sev: Error, Code: "E0801", Msg: "duplicate " + kind + " " + n, Span: sp})
		}
		seen[n] = true
	}
}
func (v *syntaxVisitor) VisitDecl(d fe.Decl) bool {
	var names []string
	switch d := d.(type) {
	case *fe.FnDecl:
		for _, a := range d.Args {
			names = append(names, a.Name)
		}
		v.duplicate(names, d.Span, "parameter")
		v.duplicate(d.Params, d.Span, "type parameter")
	case *fe.StructDecl:
		for _, f := range d.Fields {
			names = append(names, f.Name)
		}
		v.duplicate(names, d.Span, "field")
		names = nil
		for _, m := range d.Methods {
			names = append(names, m.Name)
		}
		v.duplicate(names, d.Span, "method")
	case *fe.ComponentDecl:
		for _, f := range d.Fields {
			names = append(names, f.Name)
		}
		v.duplicate(names, d.Span, "field")
		names = nil
		for _, m := range d.Methods {
			names = append(names, m.Name)
		}
		v.duplicate(names, d.Span, "method")
	case *fe.EventDecl:
		for _, f := range d.Fields {
			names = append(names, f.Name)
		}
		v.duplicate(names, d.Span, "field")
	case *fe.EnumDecl:
		for _, a := range d.Variants {
			names = append(names, a.Name)
		}
		v.duplicate(names, d.Span, "variant")
	case *fe.SystemDecl:
		for _, h := range d.Hooks {
			names = append(names, h.Kind)
		}
		v.duplicate(names, d.Span, "hook")
	}
	return true
}
func (v *syntaxVisitor) VisitExpr(e fe.Expr) bool {
	switch e := e.(type) {
	case *fe.AssignExpr:
		switch e.X.(type) {
		case *fe.IdentExpr, *fe.PathExpr, *fe.MemberExpr, *fe.IndexExpr:
		default:
			v.out = append(v.out, Diagnostic{Sev: Error, Code: "E0802", Msg: "assignment target is not assignable", Span: e.X.Pos()})
		}
	case *fe.StructExpr:
		var names []string
		for _, f := range e.Fields {
			names = append(names, f.Field)
		}
		v.duplicate(names, e.Span, "initializer")
	case *fe.LambdaExpr:
		var names []string
		for _, p := range e.Params {
			names = append(names, p.Name)
		}
		v.duplicate(names, e.Span, "parameter")
	}
	return true
}
