package sema

import fe "github.com/Chubek/JoltFX/ingagi/frontend"

// CheckImport validates import bindings. Filesystem lookup/cycles/export access
// are checked by module_system.Loader, which owns the dependency graph.
func CheckImport(f *fe.File, _ *Registry) []Diagnostic {
	var out []Diagnostic
	seen := map[string]bool{}
	for _, imp := range f.Imports {
		n := imp.Alias
		if n == "" {
			n = imp.Path.Last()
		}
		if n == "" || seen[n] {
			out = append(out, Diagnostic{Sev: Error, Code: "E0501", Msg: "duplicate or empty import alias " + n, Span: imp.Span})
		}
		seen[n] = true
	}
	for _, d := range f.Decls {
		if seen[d.DeclName()] {
			out = append(out, Diagnostic{Sev: Error, Code: "E0502", Msg: "declaration conflicts with import " + d.DeclName(), Span: d.Pos()})
		}
	}
	return out
}
