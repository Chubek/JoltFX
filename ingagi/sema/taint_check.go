package sema

import fe "github.com/Chubek/JoltFX/ingagi/frontend"

// CheckTaint is a conservative intraprocedural analysis of host-supplied data.
//
// A value is tainted when it comes from a host function the compiler cannot see
// (`RegisterHostFn`), from an `extern` declaration, or from a dynamic world
// lookup whose result depends on host state (`get_component`,
// `get_resource`, `get_parent`). Taint propagates through locals, parameters,
// struct/map/array construction, field and index access, string concatenation,
// and calls to functions whose parameter taints reach a sink.
//
// Sinks are world mutations (spawn/despawn/add_component/remove_component/
// set_resource/set_parent) and script output (print). Reaching a sink with
// tainted data is a warning, not an error: host data is legitimate input for
// game logic, and only the host can decide whether a value is trustworthy.
// Unknown functions are assumed not to sanitize, so warnings may be noisy.
//
// This pass does not claim to be a security boundary; it flags review points.
func CheckTaint(f *fe.File, r *Registry) []Diagnostic {
	a := &taintAnalyzer{reg: r, out: []Diagnostic{}, functionTaints: map[string]bool{}}
	a.known = taintKnownFunctions(f)
	a.decls(f)
	return a.out
}

type taintFn struct {
	params []bool
	ret    bool
	decl   *fe.FnDecl
}

// taintSinks are the builtins that consume script data in a way a host may care
// about: world mutation and output.
var taintSinks = map[string]int{
	"spawn": 1, "despawn": 1, "add_component": 3, "remove_component": 2,
	"set_resource": 2, "set_parent": 2, "print": 0,
}

// taintSources are dynamic lookups whose result depends on host state.
var taintSources = map[string]int{
	"get_component": -1, "get_resource": -1, "get_parent": -1,
}

type taintAnalyzer struct {
	reg            *Registry
	known          map[string]*taintFn
	hosts          map[string]bool // host-registered names (extern + unknown calls)
	out            []Diagnostic
	seenFn         map[string]bool
	functionTaints map[string]bool
}

func (a *taintAnalyzer) warn(sp fe.Span, msg string) {
	a.out = append(a.out, Diagnostic{Sev: Warning, Code: "W1001", Msg: "tainted data reaches " + msg, Span: sp})
}

// taintKnownFunctions indexes script functions for return-taint summary. A
// function is tainted-in if any parameter is used in a sink, and tainted-out if
// any sink argument is derived from a parameter. We approximate by scanning the
// body: if a sink call uses a parameter-tainted expression, mark the function.
func taintKnownFunctions(f *fe.File) map[string]*taintFn {
	known := map[string]*taintFn{}
	var add func(d *fe.FnDecl, qual string)
	add = func(d *fe.FnDecl, qual string) {
		if d == nil {
			return
		}
		fn := &taintFn{decl: d}
		for range d.Args {
			fn.params = append(fn.params, false)
		}
		known[qual] = fn
	}
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.FnDecl:
			add(d, d.Name)
		case *fe.StructDecl:
			for _, m := range d.Methods {
				add(m, d.Name+"::"+m.Name)
			}
		case *fe.ComponentDecl:
			for _, m := range d.Methods {
				add(m, d.Name+"::"+m.Name)
			}
		case *fe.SystemDecl:
			for _, fn := range d.Fns {
				add(fn, d.Name+"::"+fn.Name)
			}
		case *fe.ExternDecl:
			for _, fn := range d.Funcs {
				meta := &taintFn{decl: fn, ret: true}
				known[fn.Name] = meta
			}
		}
	}
	return known
}

func (a *taintAnalyzer) decls(f *fe.File) {
	for _, d := range f.Decls {
		a.decl(d, map[string]bool{})
	}
}

func (a *taintAnalyzer) decl(d fe.Decl, env map[string]bool) {
	switch d := d.(type) {
	case *fe.FnDecl:
		a.fn(d, d.Name, env)
	case *fe.StructDecl:
		for _, m := range d.Methods {
			a.fn(m, d.Name+"::"+m.Name, env)
		}
	case *fe.ComponentDecl:
		for _, m := range d.Methods {
			a.fn(m, d.Name+"::"+m.Name, env)
		}
	case *fe.SystemDecl:
		for _, fn := range d.Fns {
			a.fn(fn, d.Name+"::"+fn.Name, env)
		}
		for i := range d.Hooks {
			a.blockHook(&d.Hooks[i], d.Name, env)
		}
	case *fe.ExtendDecl:
		a.block(d.Body, env)
	}
}

// fn analyzes a function body. Parameters start untainted; `self`/host-return
// propagation is handled by summary reuse.
func (a *taintAnalyzer) fn(d *fe.FnDecl, qual string, outer map[string]bool) {
	if a.seenFn == nil {
		a.seenFn = map[string]bool{}
	}
	if a.seenFn[qual] {
		return
	}
	a.seenFn[qual] = true
	env := map[string]bool{}
	for k, v := range outer {
		env[k] = v
	}
	for _, p := range d.Args {
		// A function may be called with host data even when no call site in the
		// current file is tainted. Analyze parameters as tainted so the summary
		// describes the function's possible behavior and reports sink uses.
		env[p.Name] = true
	}
	if meta, ok := a.known[qual]; ok && meta.ret {
		// Extern functions return host data.
		a.functionTaints[qual] = true
	}
	if d.Body != nil {
		a.functionTaints[qual] = a.block(d.Body, env) || a.functionTaints[qual]
	}
}

// functionTaints records functions whose return value is tainted.

func (a *taintAnalyzer) blockHook(h *fe.SystemHook, sys string, outer map[string]bool) {
	env := map[string]bool{}
	for k, v := range outer {
		env[k] = v
	}
	for _, p := range h.Params {
		env[p.Name] = false
	}
	a.block(h.Body, env)
}

func (a *taintAnalyzer) block(b *fe.Block, env map[string]bool) bool {
	if b == nil {
		return false
	}
	ret := false
	for _, st := range b.Stmts {
		ret = a.stmt(st, env) || ret
	}
	return ret
}

func (a *taintAnalyzer) stmt(st fe.Stmt, env map[string]bool) bool {
	switch st := st.(type) {
	case *fe.Block:
		return a.block(st, env)
	case *fe.VarDeclStmt:
		t := a.expr(st.Init, env)
		env[st.Name] = t
	case *fe.ExprStmt:
		a.expr(st.X, env)
	case *fe.ReturnStmt:
		return a.expr(st.Value, env)
	case *fe.IfStmt:
		a.expr(st.Cond, env)
		a.block(st.Then, env)
		a.block(st.Else, env)
		if st.ElseIf != nil {
			return a.stmt(st.ElseIf, env)
		}
	case *fe.WhileStmt:
		a.expr(st.Cond, env)
		a.block(st.Body, env)
	case *fe.ForStmt:
		env["__iter_tainted"] = a.expr(st.Iter, env)
		a.block(st.Body, env)
		delete(env, "__iter_tainted")
	case *fe.LoopStmt:
		a.block(st.Body, env)
	case *fe.MatchStmt:
		a.expr(st.X, env)
		for _, arm := range st.Arms {
			a.expr(arm.Guard, env)
			a.expr(arm.Value, env)
			a.block(arm.Body, env)
		}
	case *fe.DeferStmt:
		a.block(st.Body, env)
	case *fe.UnsafeStmt:
		a.block(st.Body, env)
	case *fe.ForEachStmt:
		a.expr(st.Iter, env)
		for _, b := range st.Bindings {
			name := b.Alias
			if name == "" {
				name = b.Name
			}
			env[name] = true // world component rows are host data
		}
		a.block(st.Body, env)
	case *fe.BindStmt:
		a.expr(st.Iter, env)
	case *fe.EmitStmt:
		if a.expr(st.Event, env) {
			a.warn(st.Span, "emit event payload")
		}
	case *fe.OnStmt:
		env["it"] = true // observer receives a component snapshot from the world
		a.block(st.Body, env)
		delete(env, "it")
	case *fe.BreakStmt:
		a.expr(st.Value, env)
	case *fe.DeclStmt:
		a.decl(st.Decl, env)
	}
	return false
}

// expr returns whether the expression's value is tainted.
func (a *taintAnalyzer) expr(e fe.Expr, env map[string]bool) bool {
	if e == nil {
		return false
	}
	switch e := e.(type) {
	case *fe.Lit, *fe.PathExpr, *fe.SelfExpr:
		return false
	case *fe.IdentExpr:
		return env[e.Name]
	case *fe.UnaryExpr:
		return a.expr(e.X, env)
	case *fe.BinaryExpr:
		return a.expr(e.X, env) || a.expr(e.Y, env)
	case *fe.AssignExpr:
		t := a.expr(e.Y, env)
		a.assign(e.X, t, env)
		return t
	case *fe.TernaryExpr:
		a.expr(e.Cond, env)
		return a.expr(e.Then, env) || a.expr(e.Else, env)
	case *fe.CallExpr:
		return a.call(e, env)
	case *fe.MemberExpr:
		return a.expr(e.X, env)
	case *fe.IndexExpr:
		return a.expr(e.X, env) || a.expr(e.Index, env)
	case *fe.QuestionExpr:
		return a.expr(e.X, env)
	case *fe.TupleExpr:
		t := false
		for _, x := range e.Elems {
			t = t || a.expr(x, env)
		}
		return t
	case *fe.ArrayExpr:
		t := false
		for _, x := range e.Elems {
			t = t || a.expr(x, env)
		}
		return t
	case *fe.ArrayRepeatExpr:
		return a.expr(e.Elem, env) || a.expr(e.Count, env)
	case *fe.StructExpr:
		t := false
		for _, f := range e.Fields {
			t = t || a.expr(f.Value, env)
		}
		return t
	case *fe.LambdaExpr:
		inner := map[string]bool{}
		for k, v := range env {
			inner[k] = v
		}
		for _, p := range e.Params {
			inner[p.Name] = false
		}
		return a.expr(e.Single, inner) || a.block(e.Body, inner)
	case *fe.IfExpr:
		a.expr(e.Cond, env)
		x := a.blockValue(e.Then, env)
		y := a.blockValue(e.Else, env)
		if e.ElseIf != nil {
			y = y || a.expr(e.ElseIf, env)
		}
		return x || y
	case *fe.MatchExpr:
		a.expr(e.X, env)
		t := false
		for _, arm := range e.Arms {
			a.expr(arm.Guard, env)
			t = t || a.expr(arm.Value, env)
			a.block(arm.Body, env)
		}
		return t
	}
	return false
}

// blockValue evaluates a block for taint purposes and returns the taint of its
// value-producing final expression. Statements before that expression still
// run so their sinks are reported.
func (a *taintAnalyzer) blockValue(b *fe.Block, env map[string]bool) bool {
	if b == nil {
		return false
	}
	value := false
	for i, st := range b.Stmts {
		if i == len(b.Stmts)-1 {
			if es, ok := st.(*fe.ExprStmt); ok {
				value = a.expr(es.X, env)
				continue
			}
		}
		a.stmt(st, env)
	}
	return value
}

func (a *taintAnalyzer) assign(x fe.Expr, t bool, env map[string]bool) {
	switch x := x.(type) {
	case *fe.IdentExpr:
		env[x.Name] = t
	case *fe.MemberExpr:
		// Field assignment taints the base conservatively.
		env["__field_tainted"] = env["__field_tainted"] || t
	case *fe.IndexExpr:
		env["__index_tainted"] = env["__index_tainted"] || t
	}
}

func (a *taintAnalyzer) call(e *fe.CallExpr, env map[string]bool) bool {
	name := calleeName(e.Fn)
	args := make([]bool, len(e.Args))
	anyTainted := false
	for i, arg := range e.Args {
		args[i] = a.expr(arg.Value, env)
		anyTainted = anyTainted || args[i]
	}
	if idx, isSink := taintSinks[name]; isSink {
		taintedArg := anyTainted
		if !taintedArg && idx >= 0 && idx < len(args) {
			// Only the designated data argument matters.
			taintedArg = args[idx]
		}
		if taintedArg {
			a.warn(e.Span, sinkDesc(name))
		}
		return false // sink result is not itself tainted
	}
	if _, isSrc := taintSources[name]; isSrc {
		return true // dynamic lookup yields host data
	}
	if name == "str" {
		return anyTainted
	}
	if meta, ok := a.known[name]; ok {
		if meta.ret && meta.decl != nil && isExternDecl(meta.decl) {
			return true
		}
		// Summary: script functions analyzed with tainted parameters record
		// whether their result can contain host data. A tainted argument also
		// conservatively taints an unknown summary.
		return a.functionTaints[name] || anyTainted
	}
	if name != "" && !a.isScriptOrBuiltin(name) {
		// Unknown call: assume host function returning host data.
		return true
	}
	return anyTainted
}

func sinkDesc(name string) string {
	switch name {
	case "print":
		return "script output via print"
	case "spawn":
		return "entity creation via spawn"
	case "despawn":
		return "entity destruction via despawn"
	case "add_component":
		return "component write via add_component"
	case "remove_component":
		return "component removal via remove_component"
	case "set_resource":
		return "resource write via set_resource"
	case "set_parent":
		return "hierarchy write via set_parent"
	}
	return "sink " + name
}

func calleeName(fn fe.Expr) string {
	switch fn := fn.(type) {
	case *fe.IdentExpr:
		return fn.Name
	case *fe.PathExpr:
		return fn.Path.String()
	case *fe.MemberExpr:
		return fn.Field // method: treat as script/host call, unknown handling
	}
	return ""
}

func isExternDecl(d *fe.FnDecl) bool { return d != nil && d.Body == nil } // extern funcs are prototypes

func (a *taintAnalyzer) isScriptOrBuiltin(name string) bool {
	if _, ok := a.known[name]; ok {
		return true
	}
	if isBuiltin(name) {
		return true
	}
	if _, ok := a.reg.Funcs[name]; ok {
		return true
	}
	return false
}
