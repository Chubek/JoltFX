// Package sema provides Ingagi's draft scope analysis and symbol registry.
// Other planned analyses are explicitly reported as unavailable when requested.
// Passes never mutate the tree.
package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Severity ranks a diagnostic.
type Severity int

const (
	Note Severity = iota
	Warning
	Error
)

// Diagnostic is one semantic finding with a source span.
type Diagnostic struct {
	Sev  Severity
	Code string // stable code, e.g. "E0203"
	Msg  string
	Span fe.Span
}

func (d Diagnostic) String() string {
	sev := "note"
	switch d.Sev {
	case Warning:
		sev = "warning"
	case Error:
		sev = "error"
	}
	return d.Span.String() + ": " + sev + "[" + d.Code + "]: " + d.Msg
}

// HasErrors reports any error-severity diagnostic.
func HasErrors(ds []Diagnostic) bool {
	for _, d := range ds {
		if d.Sev == Error {
			return true
		}
	}
	return false
}

// FuncSig is a known function signature.
type FuncSig struct {
	Params int
	Ret    string
	Span   fe.Span
}

// TypeDef is a known nominal type.
type TypeDef struct {
	Kind     string // struct|component|enum|event|entity|shader|pipeline|alias
	Fields   map[string]string
	Variants []string
	Span     fe.Span
}

// SystemDef is a known system with hooks and queries.
type SystemDef struct {
	Hooks   map[string]fe.Span
	Queries []fe.SystemQuery
	Deps    []fe.SystemDep
	Span    fe.Span
}

// Registry is the module-wide symbol table built before passes run.
type Registry struct {
	Types     map[string]*TypeDef
	Funcs     map[string]*FuncSig
	Systems   map[string]*SystemDef
	Entities  map[string]fe.Span
	Events    map[string]fe.Span
	Resources map[string]fe.Span
	Consts    map[string]fe.Span
	Shaders   map[string]fe.Span
	Pipelines map[string]fe.Span
	Imports   []fe.ImportDecl
	Module    string
}

// BuildRegistry collects top-level declarations.
func BuildRegistry(f *fe.File) *Registry {
	r := &Registry{
		Types:     map[string]*TypeDef{},
		Funcs:     map[string]*FuncSig{},
		Systems:   map[string]*SystemDef{},
		Entities:  map[string]fe.Span{},
		Events:    map[string]fe.Span{},
		Resources: map[string]fe.Span{},
		Consts:    map[string]fe.Span{},
		Shaders:   map[string]fe.Span{},
		Pipelines: map[string]fe.Span{},
		Imports:   f.Imports,
		Module:    f.Module.String(),
	}
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.StructDecl:
			td := &TypeDef{Kind: "struct", Fields: map[string]string{}, Span: d.Span}
			for _, fld := range d.Fields {
				td.Fields[fld.Name] = typeName(fld.Type)
			}
			r.Types[d.Name] = td
			for _, m := range d.Methods {
				r.Funcs[d.Name+"::"+m.Name] = &FuncSig{Params: len(m.Args), Span: m.Span}
			}
		case *fe.ComponentDecl:
			td := &TypeDef{Kind: "component", Fields: map[string]string{}, Span: d.Span}
			for _, fld := range d.Fields {
				td.Fields[fld.Name] = typeName(fld.Type)
			}
			r.Types[d.Name] = td
			for _, m := range d.Methods {
				r.Funcs[d.Name+"::"+m.Name] = &FuncSig{Params: len(m.Args), Span: m.Span}
			}
		case *fe.EnumDecl:
			td := &TypeDef{Kind: "enum", Fields: map[string]string{}, Span: d.Span}
			for _, v := range d.Variants {
				td.Variants = append(td.Variants, v.Name)
			}
			r.Types[d.Name] = td
		case *fe.EventDecl:
			td := &TypeDef{Kind: "event", Fields: map[string]string{}, Span: d.Span}
			for _, fld := range d.Fields {
				td.Fields[fld.Name] = typeName(fld.Type)
			}
			r.Types[d.Name] = td
			r.Events[d.Name] = d.Span
		case *fe.FnDecl:
			r.Funcs[d.Name] = &FuncSig{Params: len(d.Args), Span: d.Span}
		case *fe.ConstDecl:
			r.Consts[d.Name] = d.Span
		case *fe.ResourceDecl:
			r.Resources[d.Name] = d.Span
		case *fe.EntityDecl:
			r.Entities[d.Name] = d.Span
		case *fe.SystemDecl:
			sd := &SystemDef{Hooks: map[string]fe.Span{}, Queries: d.Queries, Deps: d.Deps, Span: d.Span}
			for i := range d.Hooks {
				sd.Hooks[d.Hooks[i].Kind] = d.Hooks[i].Span
			}
			r.Systems[d.Name] = sd
		case *fe.ShaderDecl:
			r.Shaders[d.Name] = d.Span
			r.Types[d.Name] = &TypeDef{Kind: "shader", Fields: map[string]string{}, Span: d.Span}
		case *fe.PipelineDecl:
			r.Pipelines[d.Name] = d.Span
			r.Types[d.Name] = &TypeDef{Kind: "pipeline", Fields: map[string]string{}, Span: d.Span}
		case *fe.TypeAliasDecl:
			r.Types[d.Name] = &TypeDef{Kind: "alias", Fields: map[string]string{}, Span: d.Span}
		}
	}
	return r
}

// typeName renders a type expression's head for messages.
func typeName(t fe.Type) string {
	switch t := t.(type) {
	case *fe.TypeName:
		return t.Path.Last()
	case *fe.ArrayType:
		return "[" + typeName(t.Elem) + "]"
	case *fe.RefType:
		return "&" + typeName(t.Elem)
	case *fe.PtrType:
		return "*" + typeName(t.Elem)
	case *fe.TupleType:
		return "tuple"
	case *fe.FnType:
		return "fn"
	case nil:
		return "?"
	}
	return "?"
}

// Options selects passes.
type Options struct {
	NoImport, NoScope, NoType, NoECS, NoShader         bool
	NoSyntax, NoLogic, NoOverflow, NoSecurity, NoTaint bool
}

// DefaultOptions enables implemented passes. The zero Options value also
// requests planned passes; Check reports those as unavailable, not successful.
func DefaultOptions() Options {
	return Options{NoImport: true, NoType: true, NoECS: true, NoShader: true,
		NoSyntax: true, NoLogic: true, NoOverflow: true, NoSecurity: true, NoTaint: true}
}

// Check runs the selected passes and returns merged diagnostics.
func Check(f *fe.File, o Options) []Diagnostic {
	r := BuildRegistry(f)
	var out []Diagnostic
	if !o.NoScope {
		out = append(out, CheckScope(f, r)...)
	}
	for _, pass := range []struct {
		name     string
		disabled bool
	}{
		{"syntax", o.NoSyntax}, {"type", o.NoType}, {"ECS", o.NoECS},
		{"shader", o.NoShader}, {"imports", o.NoImport}, {"logic", o.NoLogic},
		{"overflow", o.NoOverflow}, {"security", o.NoSecurity}, {"taint", o.NoTaint},
	} {
		if !pass.disabled {
			out = append(out, Diagnostic{Sev: Error, Code: "E0001",
				Msg: pass.name + " analysis is not implemented"})
		}
	}
	return out
}
