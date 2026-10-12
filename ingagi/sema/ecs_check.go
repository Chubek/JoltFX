package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"sort"
)

// CheckECS checks component kinds, query conflicts and system dependencies.
func CheckECS(f *fe.File, r *Registry) []Diagnostic {
	v := &ecsVisitor{r: r}
	fe.Walk(v, f)
	// Edges always mean prerequisite -> dependent; names are sorted to keep
	// diagnostics deterministic even though the registry uses maps.
	edges := map[string][]string{}
	var names []string
	for n := range r.Systems {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		for _, d := range r.Systems[n].Deps {
			if d.Kind == "parallel" {
				continue
			}
			if r.Systems[d.Target] == nil {
				v.error("E0303", "unknown system "+d.Target, d.Span)
				continue
			}
			from, to := n, d.Target
			if d.Kind == "after" {
				from, to = to, from
			}
			edges[from] = append(edges[from], to)
		}
	}
	state := map[string]int{}
	var visit func(string) bool
	visit = func(n string) bool {
		if state[n] == 1 {
			return true
		}
		if state[n] == 2 {
			return false
		}
		state[n] = 1
		for _, next := range edges[n] {
			if visit(next) {
				return true
			}
		}
		state[n] = 2
		return false
	}
	for _, n := range names {
		if visit(n) {
			v.error("E0304", "cycle in system ordering involving "+n, r.Systems[n].Span)
			break
		}
	}
	return v.out
}

type ecsVisitor struct {
	fe.BaseVisitor
	r   *Registry
	out []Diagnostic
}

func (v *ecsVisitor) error(code, msg string, sp fe.Span) {
	v.out = append(v.out, Diagnostic{Sev: Error, Code: code, Msg: msg, Span: sp})
}
func (v *ecsVisitor) component(n string, sp fe.Span) {
	if t := v.r.Types[n]; t == nil || t.Kind != "component" {
		v.error("E0301", "expected component type "+n, sp)
	}
}
func (v *ecsVisitor) query(with, without, optional []fe.QueryComp) {
	seen := map[string]bool{}
	for _, list := range [][]fe.QueryComp{with, without, optional} {
		for _, q := range list {
			n := q.Type.String()
			v.component(n, q.Span)
			if seen[n] {
				v.error("E0302", "duplicate or contradictory query component "+n, q.Span)
			}
			seen[n] = true
		}
	}
}
func (v *ecsVisitor) VisitDecl(d fe.Decl) bool {
	switch d := d.(type) {
	case *fe.EntityDecl:
		for _, a := range d.Attachments {
			v.component(a.Type.String(), a.Span)
		}
	case *fe.SystemDecl:
		for _, q := range d.Queries {
			v.query(q.With, q.Without, q.Optional)
		}
	}
	return true
}
func (v *ecsVisitor) VisitStmt(s fe.Stmt) bool {
	switch s := s.(type) {
	case *fe.ForEachStmt:
		seen := map[string]bool{}
		for _, b := range s.Bindings {
			v.component(b.Name, b.Span)
			if seen[b.Name] {
				v.error("E0302", "duplicate query component "+b.Name, b.Span)
			}
			seen[b.Name] = true
		}
	case *fe.OnStmt:
		v.component(s.Comp.String(), s.Span)
	case *fe.BindStmt:
		v.query(s.With, s.Without, nil)
	case *fe.EmitStmt:
		if e, ok := s.Event.(*fe.StructExpr); ok {
			if t := v.r.Types[e.Type.String()]; t == nil || t.Kind != "event" {
				v.error("E0305", "expected event type "+e.Type.String(), e.Span)
			}
		}
	}
	return true
}
