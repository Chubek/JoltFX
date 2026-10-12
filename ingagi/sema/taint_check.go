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
	if r == nil {
		r = BuildRegistry(f)
	}
	a := &taintAnalyzer{reg: r, out: []Diagnostic{}, functionTaints: map[string]bool{}}
	a.known = taintKnownFunctions(f)
	// Solve return summaries to a fixed point before reporting. This handles
	// forward declarations and recursive call graphs without declaration-order
	// dependence. Summaries only grow, so at most one change per function is needed.
	for i := 0; i <= len(a.known); i++ {
		before := 0
		for _, t := range a.functionTaints {
			if t {
				before++
			}
		}
		a.seenFn = nil
		a.decls(f)
		a.out = nil
		a.warned = nil
		after := 0
		for _, t := range a.functionTaints {
			if t {
				after++
			}
		}
		if before == after {
			break
		}
	}
	a.seenFn = nil
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
	warned         map[string]bool
}

func (a *taintAnalyzer) warn(sp fe.Span, msg string) {
	if a.warned == nil {
		a.warned = map[string]bool{}
	}
	key := sp.String() + msg
	if a.warned[key] {
		return
	}
	a.warned[key] = true
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
	globals := map[string]bool{}
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.ConstDecl:
			globals[d.Name] = a.expr(d.Value, globals)
		case *fe.ResourceDecl:
			globals[d.Name] = a.expr(d.Value, globals)
		}
	}
	for _, d := range f.Decls {
		a.decl(d, copyTaints(globals))
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

func copyTaints(env map[string]bool) map[string]bool {
	out := make(map[string]bool, len(env))
	for k, v := range env {
		out[k] = v
	}
	return out
}

func joinTaints(env map[string]bool, branches ...map[string]bool) bool {
	changed := false
	for k := range env {
		for _, b := range branches {
			if b[k] && !env[k] {
				env[k] = true
				changed = true
			}
		}
	}
	return changed
}

func (a *taintAnalyzer) loop(body *fe.Block, cond fe.Expr, env map[string]bool) bool {
	ret := false
	for {
		inner := copyTaints(env)
		a.expr(cond, inner)
		ret = a.block(body, inner) || ret
		if !joinTaints(env, inner) {
			return ret
		}
	}
}

func (a *taintAnalyzer) block(b *fe.Block, env map[string]bool) bool {
	if b == nil {
		return false
	}
	ret := false
	// Restore block-local declarations, including shadowed outer bindings;
	// assignments to outer locals remain visible to the caller.
	saved, declared := map[string]bool{}, map[string]bool{}
	defer func() {
		for name, existed := range declared {
			if existed {
				env[name] = saved[name]
			} else {
				delete(env, name)
			}
		}
	}()
	for _, st := range b.Stmts {
		if d, ok := st.(*fe.VarDeclStmt); ok {
			if _, seen := declared[d.Name]; !seen {
				saved[d.Name], declared[d.Name] = env[d.Name], false
				_, declared[d.Name] = env[d.Name]
			}
		}
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
		x, y := copyTaints(env), copyTaints(env)
		ret := a.block(st.Then, x)
		ret = a.block(st.Else, y) || ret
		if st.ElseIf != nil {
			ret = a.stmt(st.ElseIf, y) || ret
		}
		joinTaints(env, x, y)
		return ret
	case *fe.WhileStmt:
		return a.loop(st.Body, st.Cond, env)
	case *fe.ForStmt:
		t := a.expr(st.Iter, env)
		inner := copyTaints(env)
		bindTaintPattern(st.Pat, t, inner)
		ret := a.loop(st.Body, nil, inner)
		restorePattern(st.Pat, env, inner)
		joinTaints(env, inner)
		return ret
	case *fe.LoopStmt:
		return a.loop(st.Body, nil, env)
	case *fe.MatchStmt:
		t := a.expr(st.X, env)
		ret := false
		for _, arm := range st.Arms {
			inner := copyTaints(env)
			bindTaintPattern(arm.Pat, t, inner)
			a.expr(arm.Guard, inner)
			a.expr(arm.Value, inner)
			ret = a.block(arm.Body, inner) || ret
			restorePattern(arm.Pat, env, inner)
			joinTaints(env, inner)
		}
		return ret
	case *fe.DeferStmt:
		a.block(st.Body, env)
	case *fe.UnsafeStmt:
		return a.block(st.Body, env)
	case *fe.ForEachStmt:
		a.expr(st.Iter, env)
		inner := copyTaints(env)
		for _, b := range st.Bindings {
			name := b.Alias
			if name == "" {
				name = b.Name
			}
			inner[name] = true // world component rows are host data
		}
		ret := a.loop(st.Body, nil, inner)
		for _, b := range st.Bindings {
			name := b.Alias
			if name == "" {
				name = b.Name
			}
			inner[name] = env[name]
		}
		joinTaints(env, inner)
		return ret
	case *fe.BindStmt:
		a.expr(st.Iter, env)
	case *fe.EmitStmt:
		if a.expr(st.Event, env) {
			a.warn(st.Span, "emit event payload")
		}
	case *fe.OnStmt:
		inner := copyTaints(env)
		inner["it"] = true // observer receives a component snapshot from the world
		a.block(st.Body, inner)
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
	case *fe.Lit:
		return false
	case *fe.PathExpr:
		return env[e.Path.String()]
	case *fe.SelfExpr:
		return env["self"]
	case *fe.IdentExpr:
		return env[e.Name]
	case *fe.UnaryExpr:
		return a.expr(e.X, env)
	case *fe.BinaryExpr:
		x := a.expr(e.X, env)
		if e.Op == "&&" || e.Op == "||" {
			inner := copyTaints(env)
			y := a.expr(e.Y, inner)
			joinTaints(env, inner)
			return x || y
		}
		y := a.expr(e.Y, env)
		return x || y
	case *fe.AssignExpr:
		old := false
		if e.Op != "=" {
			old = a.expr(e.X, env)
		}
		t := a.expr(e.Y, env)
		t = old || t
		a.assign(e.X, t, env)
		return t
	case *fe.TernaryExpr:
		a.expr(e.Cond, env)
		x, y := copyTaints(env), copyTaints(env)
		t, u := a.expr(e.Then, x), a.expr(e.Else, y)
		joinTaints(env, x, y)
		return t || u
	case *fe.CallExpr:
		return a.call(e, env)
	case *fe.MemberExpr:
		return a.expr(e.X, env)
	case *fe.IndexExpr:
		x, y := a.expr(e.X, env), a.expr(e.Index, env)
		return x || y
	case *fe.QuestionExpr:
		return a.expr(e.X, env)
	case *fe.TupleExpr:
		t := false
		for _, x := range e.Elems {
			t = a.expr(x, env) || t
		}
		return t
	case *fe.ArrayExpr:
		t := false
		for _, x := range e.Elems {
			t = a.expr(x, env) || t
		}
		return t
	case *fe.ArrayRepeatExpr:
		x, y := a.expr(e.Elem, env), a.expr(e.Count, env)
		return x || y
	case *fe.StructExpr:
		t := false
		for _, f := range e.Fields {
			t = a.expr(f.Value, env) || t
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
		x, y := a.expr(e.Single, inner), a.block(e.Body, inner)
		return x || y
	case *fe.IfExpr:
		a.expr(e.Cond, env)
		left, right := copyTaints(env), copyTaints(env)
		x := a.blockValue(e.Then, left)
		y := a.blockValue(e.Else, right)
		if e.ElseIf != nil {
			y = a.expr(e.ElseIf, right) || y
		}
		joinTaints(env, left, right)
		return x || y
	case *fe.MatchExpr:
		input := a.expr(e.X, env)
		t := false
		for _, arm := range e.Arms {
			inner := copyTaints(env)
			bindTaintPattern(arm.Pat, input, inner)
			a.expr(arm.Guard, inner)
			t = a.expr(arm.Value, inner) || t
			t = a.blockValue(arm.Body, inner) || t
			restorePattern(arm.Pat, env, inner)
			joinTaints(env, inner)
		}
		return t
	}
	return false
}

func bindTaintPattern(p fe.Pattern, t bool, env map[string]bool) {
	switch p := p.(type) {
	case *fe.BindPat:
		env[p.Name] = t
	case *fe.TuplePat:
		for _, e := range p.Elems {
			bindTaintPattern(e, t, env)
		}
	case *fe.ArrayPat:
		for _, e := range p.Elems {
			bindTaintPattern(e, t, env)
		}
		if p.Rest != "" {
			env[p.Rest] = t
		}
	case *fe.StructPat:
		for _, f := range p.Fields {
			if f.Pat == nil {
				env[f.Field] = t
			} else {
				bindTaintPattern(f.Pat, t, env)
			}
		}
	case *fe.OrPat:
		for _, e := range p.Alts {
			bindTaintPattern(e, t, env)
		}
	}
}

func restorePattern(p fe.Pattern, outer, inner map[string]bool) {
	names := map[string]bool{}
	bindTaintPattern(p, false, names)
	for name := range names {
		inner[name] = outer[name]
	}
}

// blockValue evaluates a block for taint purposes and returns the taint of its
// value-producing final expression. Statements before that expression still
// run so their sinks are reported.
func (a *taintAnalyzer) blockValue(b *fe.Block, env map[string]bool) bool {
	if b == nil {
		return false
	}
	copy := *b
	copy.Stmts = append([]fe.Stmt(nil), b.Stmts...)
	if n := len(copy.Stmts); n > 0 {
		if es, ok := copy.Stmts[n-1].(*fe.ExprStmt); ok {
			copy.Stmts[n-1] = &fe.ReturnStmt{Value: es.X, Span: es.Span}
		}
	}
	return a.block(&copy, env)
}

func (a *taintAnalyzer) assign(x fe.Expr, t bool, env map[string]bool) {
	switch x := x.(type) {
	case *fe.IdentExpr:
		env[x.Name] = t
	case *fe.MemberExpr:
		// Conservatively taint the actual aggregate, not a synthetic unrelated local.
		a.assign(x.X, a.expr(x.X, env) || t, env)
	case *fe.IndexExpr:
		a.expr(x.Index, env)
		a.assign(x.X, a.expr(x.X, env) || t, env)
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
