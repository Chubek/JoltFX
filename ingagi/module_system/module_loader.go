// Package module_system resolves source imports and assembles an owned,
// namespaced AST. Each dependency is initialized once, before its importers.
package module_system

import (
	"errors"
	"fmt"
	"io/fs"
	"path"
	"reflect"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Loader resolves a::b as a/b.ing below FS. Modules may omit their module
// declaration; an explicit declaration must match the imported path. Dependencies
// expose public declarations only. The root file retains its unqualified entries.
// Load uses invocation-local caches, so reusing a Loader observes changed files.
type Loader struct{ FS fs.FS }

type unit struct {
	file    *fe.File
	name    string
	exports map[string]bool
}

func (l *Loader) Load(filename string) (*fe.File, error) {
	if l == nil || l.FS == nil {
		return nil, fmt.Errorf("ingagi: module loader has no filesystem")
	}
	if !fs.ValidPath(filename) {
		return nil, fmt.Errorf("ingagi: invalid source path %q", filename)
	}
	units := map[string]*unit{}
	active := map[string]bool{}
	var order []*unit
	var load func(string, string) (*unit, error)
	load = func(filename, name string) (*unit, error) {
		if active[filename] {
			return nil, fmt.Errorf("ingagi: import cycle at %s", filename)
		}
		if u := units[filename]; u != nil {
			return u, nil
		}
		data, err := fs.ReadFile(l.FS, filename)
		if err != nil {
			return nil, fmt.Errorf("ingagi: module %s: %w", filename, err)
		}
		parsed := fe.Parse(string(data))
		if !parsed.OK() {
			var errs []error
			for _, err := range parsed.Errors {
				errs = append(errs, err)
			}
			return nil, fmt.Errorf("%s: %w", filename, errors.Join(errs...))
		}
		if name != "" && len(parsed.File.Module.Parts) > 0 && strings.TrimPrefix(parsed.File.Module.String(), "::") != name {
			return nil, fmt.Errorf("ingagi: %s declares module %s, expected %s", filename, parsed.File.Module.String(), name)
		}
		u := &unit{file: parsed.File, name: name, exports: map[string]bool{}}
		for _, d := range u.file.Decls {
			if public(d) {
				u.exports[d.DeclName()] = true
			}
		}
		units[filename] = u
		active[filename] = true
		imports := map[string]*unit{}
		for _, imp := range u.file.Imports {
			alias := imp.Alias
			if alias == "" {
				alias = imp.Path.Last()
			}
			if alias == "" || imports[alias] != nil {
				return nil, fmt.Errorf("%s:%s: duplicate import alias %q", filename, imp.Span, alias)
			}
			parts := make([]string, len(imp.Path.Parts))
			for i, p := range imp.Path.Parts {
				parts[i] = p.Name
			}
			dep, err := load(path.Join(parts...)+".ing", strings.TrimPrefix(imp.Path.String(), "::"))
			if err != nil {
				return nil, fmt.Errorf("%s:%s: %w", filename, imp.Span, err)
			}
			imports[alias] = dep
		}
		q := &qualifier{names: map[string]string{}, imports: imports}
		for _, d := range u.file.Decls {
			n := d.DeclName()
			if n == "extern" || n == "extend" {
				continue
			}
			if _, exists := q.names[n]; exists {
				return nil, fmt.Errorf("%s:%s: duplicate declaration %s", filename, d.Pos(), n)
			}
			if imports[n] != nil {
				return nil, fmt.Errorf("%s:%s: declaration conflicts with import %s", filename, d.Pos(), n)
			}
			q.names[n] = n
			if name != "" {
				q.names[n] = name + "::" + n
			}
		}
		for _, d := range u.file.Decls {
			q.walk(reflect.ValueOf(d), map[string]bool{})
			if q.err != nil {
				return nil, fmt.Errorf("%s: %w", filename, q.err)
			}
			// Only module-level declarations are renamed. Fields, method names,
			// parameters and lexical declarations keep their original names.
			v := reflect.ValueOf(d).Elem().FieldByName("Name")
			if v.IsValid() && v.CanSet() {
				v.SetString(q.names[d.DeclName()])
			}
		}
		u.file.Imports = nil
		active[filename] = false
		order = append(order, u)
		return u, nil
	}
	root, err := load(filename, "")
	if err != nil {
		return nil, err
	}
	out := &fe.File{Module: root.file.Module, Span: root.file.Span}
	for _, u := range order {
		out.Decls = append(out.Decls, u.file.Decls...)
	}
	return out, nil
}

func public(d fe.Decl) bool {
	v := reflect.ValueOf(d).Elem().FieldByName("Vis")
	return v.IsValid() && v.String() == string(fe.VisPublic)
}

type qualifier struct {
	names   map[string]string
	imports map[string]*unit
	err     error
}

func cloneScope(s map[string]bool) map[string]bool {
	r := make(map[string]bool, len(s))
	for k, v := range s {
		r[k] = v
	}
	return r
}

func (q *qualifier) resolve(n string, sp fe.Span, s map[string]bool) string {
	parts := strings.Split(strings.TrimPrefix(n, "::"), "::")
	if len(parts) == 1 && s[n] {
		return n
	}
	if u := q.imports[parts[0]]; u != nil {
		if len(parts) < 2 || !u.exports[parts[1]] {
			q.err = fmt.Errorf("%s: %s is not exported by module %s", sp, n, u.name)
			return n
		}
		return u.name + "::" + strings.Join(parts[1:], "::")
	}
	if renamed, ok := q.names[parts[0]]; ok {
		parts[0] = renamed
		return strings.Join(parts, "::")
	}
	return n
}

func (q *qualifier) path(p *fe.Path, s map[string]bool) {
	if len(p.Parts) == 0 {
		return
	}
	n := q.resolve(p.String(), p.Span, s)
	parts := strings.Split(strings.TrimPrefix(n, "::"), "::")
	p.Global = false
	p.Parts = make([]fe.Ident, len(parts))
	for i, n := range parts {
		p.Parts[i] = fe.Ident{Name: n, Span: p.Span}
	}
}

// walk rewrites an owned parser tree. Reflection handles the structural nodes;
// lexical constructs are explicit so shadowing and branch-local bindings are
// preserved. No storage supplied by callers is mutated by Load.
func (q *qualifier) walk(v reflect.Value, s map[string]bool) {
	if !v.IsValid() || q.err != nil {
		return
	}
	if v.Kind() == reflect.Interface {
		if !v.IsNil() {
			q.walk(v.Elem(), s)
		}
		return
	}
	if v.Kind() == reflect.Pointer {
		if v.IsNil() {
			return
		}
		switch n := v.Interface().(type) {
		case *fe.TypeName:
			q.path(&n.Path, nil)
			q.walk(reflect.ValueOf(n.Args), nil)
			return
		case *fe.IdentExpr:
			n.Name = q.resolve(n.Name, n.Span, s)
			return
		case *fe.Block:
			inner := cloneScope(s)
			// Nested function declarations are visible throughout the block.
			for _, st := range n.Stmts {
				if d, ok := st.(*fe.DeclStmt); ok {
					inner[d.Decl.DeclName()] = true
				}
			}
			for _, st := range n.Stmts {
				q.walk(reflect.ValueOf(st), inner)
				if d, ok := st.(*fe.VarDeclStmt); ok {
					inner[d.Name] = true
				}
			}
			return
		case *fe.FnDecl:
			inner := cloneScope(s)
			for _, a := range n.Args {
				inner[a.Name] = true
			}
			q.walk(reflect.ValueOf(n.Args), inner)
			q.walk(reflect.ValueOf(n.Ret), s)
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		case *fe.LambdaExpr:
			inner := cloneScope(s)
			for _, a := range n.Params {
				inner[a.Name] = true
			}
			q.walk(reflect.ValueOf(n.Params), inner)
			q.walk(reflect.ValueOf(n.Ret), s)
			q.walk(reflect.ValueOf(n.Single), inner)
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		case *fe.ForStmt:
			q.walk(reflect.ValueOf(n.Iter), s)
			inner := cloneScope(s)
			bind(n.Pat, inner)
			q.walk(reflect.ValueOf(n.Pat), inner)
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		case *fe.ForEachStmt:
			q.walk(reflect.ValueOf(n.Iter), s)
			inner := cloneScope(s)
			for i := range n.Bindings {
				b := &n.Bindings[i]
				alias := b.Alias
				if alias == "" {
					alias = b.Name
					b.Alias = alias
				}
				b.Name = q.resolve(b.Name, b.Span, s)
				inner[alias] = true
			}
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		case *fe.StructExpr:
			q.path(&n.Type, s)
			for i := range n.Fields {
				f := &n.Fields[i]
				if f.Value == nil {
					f.Value = &fe.IdentExpr{Name: f.Field, Span: f.Span}
				}
				q.walk(reflect.ValueOf(f.Value), s)
			}
			return
		case *fe.BindStmt:
			n.Pipeline = q.resolve(n.Pipeline, n.Span, s)
		case *fe.OnStmt:
			inner := cloneScope(s)
			inner["entity"] = true
			q.path(&n.Comp, s)
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		}
		q.walk(v.Elem(), s)
		return
	}
	if v.CanAddr() {
		switch n := v.Addr().Interface().(type) {
		case *fe.Path:
			q.path(n, s)
			return
		case *fe.MatchArm:
			inner := cloneScope(s)
			bind(n.Pat, inner)
			q.walk(reflect.ValueOf(n.Pat), inner)
			q.walk(reflect.ValueOf(n.Guard), inner)
			q.walk(reflect.ValueOf(n.Value), inner)
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		case *fe.SystemHook:
			inner := cloneScope(s)
			for _, a := range n.Params {
				inner[a.Name] = true
			}
			q.walk(reflect.ValueOf(n.Params), inner)
			q.walk(reflect.ValueOf(n.Body), inner)
			return
		case *fe.SystemDep:
			if n.Kind != "parallel" {
				n.Target = q.resolve(n.Target, n.Span, s)
			}
			return
		}
	}
	switch v.Kind() {
	case reflect.Struct:
		for i := 0; i < v.NumField(); i++ {
			q.walk(v.Field(i), s)
		}
	case reflect.Slice:
		for i := 0; i < v.Len(); i++ {
			q.walk(v.Index(i), s)
		}
	case reflect.Map:
		// Map values (pipeline paths/options) aren't addressable. Rewrite a
		// temporary owned value, then install it back into the map.
		for _, k := range v.MapKeys() {
			copy := reflect.New(v.Type().Elem()).Elem()
			copy.Set(v.MapIndex(k))
			q.walk(copy, s)
			v.SetMapIndex(k, copy)
		}
	}
}

func bind(p fe.Pattern, s map[string]bool) {
	switch p := p.(type) {
	case *fe.BindPat:
		s[p.Name] = true
	case *fe.TuplePat:
		for _, p := range p.Elems {
			bind(p, s)
		}
	case *fe.ArrayPat:
		for _, p := range p.Elems {
			bind(p, s)
		}
		if p.HasRest {
			s[p.Rest] = true
		}
	case *fe.StructPat:
		for _, f := range p.Fields {
			if f.Pat == nil {
				s[f.Field] = true
			} else {
				bind(f.Pat, s)
			}
		}
	case *fe.OrPat:
		for _, p := range p.Alts {
			bind(p, s)
		}
	}
}
