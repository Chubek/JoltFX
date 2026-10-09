package runtime

import (
	"fmt"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Module is a compiled Ingagi source file: executable tape plus the
// metadata the engine needs (types, entities, systems, shaders).
type Module struct {
	Name        string
	Tape        *Tape
	Types       map[string]*TypeInfo
	Entities    map[string]*fe.EntityDecl
	Systems     []*fe.SystemDecl
	SystemNames []string
	Hooks       []HookInfo
	Shaders     []string
	Pipelines   []string
	Events      []string
	Resources   map[string]Value
	Params      map[string]int // entry name -> declared parameter count
}

// HookInfo maps a system hook to its tape entry without AST.
type HookInfo struct {
	System string
	Kind   string
	Entry  string
	Event  string // on_event parameter type, "" otherwise
}

// TypeInfo describes a struct/component/enum/event type for defaults,
// reflection and component conversion.
type TypeInfo struct {
	Kind     string // struct|component|enum|event|entity
	Fields   []TypeField
	Defaults map[string]Value
	Variants []string
	Methods  map[string]string // method name -> qualified entry name
}

// TypeField is one named field with its default AST (evaluated at load).
type TypeField struct {
	Name    string
	Default fe.Expr
}

// Compiler lowers a frontend File to a Module. It reports structural
// problems (unknown names are runtime errors, keeping scripts dynamically
// relinkable for hot reload).
type Compiler struct {
	b         *Builder
	pool      *ConstPool
	module    *Module
	scopes    []*cscope
	loops     []*loopCtx
	funcName  string
	funcEntry map[string]*fe.FnDecl
	nilFix    []int // `?` propagation fixups in the current function
	lambdaSeq int
	hiddenSeq int
	slotTop   int // function-wide local slot counter
	params    map[string]int
	errors    []string
}

type cscope struct {
	locals      map[string]int
	funcBd      bool // function boundary (closures stop here)
	ups         []string
	savedSlots  int
	savedName   string
	savedNilFix []int
	savedLoops  []*loopCtx
}

type loopCtx struct {
	breaks    []int
	continues []int
	result    int // result slot, -1 when none
}

// Compile compiles a file into a Module.
func Compile(f *fe.File) (*Module, []string) {
	pool := NewConstPool()
	c := &Compiler{
		b:         NewBuilder(pool),
		pool:      pool,
		module:    &Module{Types: map[string]*TypeInfo{}, Entities: map[string]*fe.EntityDecl{}, Resources: map[string]Value{}, Params: map[string]int{}},
		funcEntry: map[string]*fe.FnDecl{},
		params:    map[string]int{},
	}
	if f.Module.Parts != nil {
		c.module.Name = f.Module.String()
	}
	c.collect(f)
	c.compileInit(f)
	c.compilePrefabs(f)
	for _, d := range f.Decls {
		if fn, ok := d.(*fe.FnDecl); ok {
			c.compileFn(fn, fn.Name)
		}
	}
	// Struct/component methods become qualified entries.
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.StructDecl:
			for _, m := range d.Methods {
				c.compileFn(m, d.Name+"::"+m.Name)
			}
		case *fe.ComponentDecl:
			for _, m := range d.Methods {
				c.compileFn(m, d.Name+"::"+m.Name)
			}
		case *fe.SystemDecl:
			for _, fn := range d.Fns {
				c.compileFn(fn, d.Name+"::"+fn.Name)
			}
			for i := range d.Hooks {
				c.compileHook(d.Name, &d.Hooks[i])
			}
		}
	}
	c.b.Emit(OpHalt, 0, 0)
	c.module.Tape = c.b.Build()
	c.module.Params = c.params
	// Hook index + system names for the engine and AOT.
	for _, d := range f.Decls {
		if sys, ok := d.(*fe.SystemDecl); ok {
			c.module.SystemNames = append(c.module.SystemNames, sys.Name)
			for i := range sys.Hooks {
				h := &sys.Hooks[i]
				hi := HookInfo{System: sys.Name, Kind: h.Kind, Entry: sys.Name + "::" + h.Kind}
				if h.Kind == "on_event" && len(h.Params) > 0 {
					hi.Event = typeLast(h.Params[0].Type)
				}
				c.module.Hooks = append(c.module.Hooks, hi)
			}
		}
	}
	return c.module, c.errors
}

// compileHook compiles a system hook body as its own entry so the engine
// can drive phases (startup/update/fixed_update/…) directly.
func (c *Compiler) compileHook(sys string, h *fe.SystemHook) {
	qual := sys + "::" + h.Kind
	c.b.MarkEntry(qual)
	c.pushScope(true)
	c.funcName = qual
	c.nilFix = nil
	c.params[qual] = len(h.Params)
	for _, a := range h.Params {
		c.declare(a.Name)
	}
	if h.Body != nil {
		c.compileBlockValue(h.Body, false)
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	for _, at := range c.nilFix {
		c.b.Patch(at, c.b.Pos())
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	c.nilFix = nil
	c.popScope()
}

func (c *Compiler) errf(sp fe.Span, format string, args ...any) {
	c.errors = append(c.errors, fmt.Sprintf("%s: %s", sp, fmt.Sprintf(format, args...)))
}

// collect gathers type metadata before code generation so field defaults
// and method sets are visible everywhere (order-independent).
func (c *Compiler) collect(f *fe.File) {
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.StructDecl:
			ti := &TypeInfo{Kind: "struct", Defaults: map[string]Value{}, Methods: map[string]string{}}
			for _, fld := range d.Fields {
				ti.Fields = append(ti.Fields, TypeField{Name: fld.Name})
			}
			for _, m := range d.Methods {
				ti.Methods[m.Name] = d.Name + "::" + m.Name
			}
			c.module.Types[d.Name] = ti
		case *fe.ComponentDecl:
			ti := &TypeInfo{Kind: "component", Defaults: map[string]Value{}, Methods: map[string]string{}}
			for _, fld := range d.Fields {
				ti.Fields = append(ti.Fields, TypeField{Name: fld.Name, Default: fld.Default})
			}
			for _, m := range d.Methods {
				ti.Methods[m.Name] = d.Name + "::" + m.Name
			}
			c.module.Types[d.Name] = ti
		case *fe.EnumDecl:
			ti := &TypeInfo{Kind: "enum", Defaults: map[string]Value{}, Methods: map[string]string{}}
			for _, v := range d.Variants {
				ti.Variants = append(ti.Variants, v.Name)
			}
			c.module.Types[d.Name] = ti
		case *fe.EventDecl:
			ti := &TypeInfo{Kind: "event", Defaults: map[string]Value{}, Methods: map[string]string{}}
			for _, fld := range d.Fields {
				ti.Fields = append(ti.Fields, TypeField{Name: fld.Name, Default: fld.Default})
			}
			c.module.Types[d.Name] = ti
			c.module.Events = append(c.module.Events, d.Name)
		case *fe.EntityDecl:
			c.module.Entities[d.Name] = d
		case *fe.SystemDecl:
			c.module.Systems = append(c.module.Systems, d)
		case *fe.ShaderDecl:
			c.module.Shaders = append(c.module.Shaders, d.Name)
		case *fe.PipelineDecl:
			c.module.Pipelines = append(c.module.Pipelines, d.Name)
		case *fe.FnDecl:
			c.funcEntry[d.Name] = d
		}
	}
}

// compileInit emits the <init> entry: consts, resources and component
// field defaults evaluate once at load.
func (c *Compiler) compileInit(f *fe.File) {
	c.b.MarkEntry("<init>")
	c.pushScope(true)
	c.funcName = "<init>"
	for _, d := range f.Decls {
		switch d := d.(type) {
		case *fe.ConstDecl:
			c.compileExpr(d.Value)
			c.b.Emit(OpStoreGlobal, 0, c.pool.InternString(d.Name))
		case *fe.ResourceDecl:
			if d.Value != nil {
				c.compileExpr(d.Value)
			} else {
				c.b.Emit(OpPushNil, 0, 0)
			}
			c.b.Emit(OpStoreGlobal, 0, c.pool.InternString("resource::"+d.Name))
		}
	}
	// Component/event default snapshots for MakeStruct fill-in.
	for name, ti := range c.module.Types {
		for _, fld := range ti.Fields {
			if fld.Default == nil {
				continue
			}
			slot := c.declareHidden()
			_ = slot
			c.compileExpr(fld.Default)
			c.b.Emit(OpStoreGlobal, 0, c.pool.InternString("default::"+name+"::"+fld.Name))
		}
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	c.popScope()
}

// ---------------------------------------------------------------------------
// Scopes
// ---------------------------------------------------------------------------

func (c *Compiler) pushScope(funcBd bool) {
	s := &cscope{locals: map[string]int{}, funcBd: funcBd}
	if funcBd {
		// Function bodies have independent slots, propagation targets and loops.
		s.savedSlots, s.savedName = c.slotTop, c.funcName
		s.savedNilFix, s.savedLoops = c.nilFix, c.loops
		c.slotTop = 0
		c.nilFix, c.loops = nil, nil
	}
	c.scopes = append(c.scopes, s)
}

// popScope pops the innermost block. Only function boundaries own slot
// numbering, so only they restore slotTop; plain blocks share the
// function's counter (slots are never reused, which keeps jumps valid).
func (c *Compiler) popScope() {
	sc := c.scopes[len(c.scopes)-1]
	c.scopes = c.scopes[:len(c.scopes)-1]
	if sc.funcBd {
		c.slotTop, c.funcName = sc.savedSlots, sc.savedName
		c.nilFix, c.loops = sc.savedNilFix, sc.savedLoops
	}
}

// declare allocates a function-wide local slot. Slots are never reused
// within a function: simple, stable, and debuggable.
func (c *Compiler) declare(name string) int {
	s := c.scopes[len(c.scopes)-1]
	slot := c.slotTop
	c.slotTop++
	s.locals[name] = slot
	return slot
}

// declareHidden allocates an anonymous slot.
func (c *Compiler) declareHidden() int {
	c.hiddenSeq++
	return c.declare("\x00" + itoa(int64(c.hiddenSeq)))
}

// fnBase returns the index of the innermost enclosing function scope.
func (c *Compiler) fnBase() int {
	for i := len(c.scopes) - 1; i >= 0; i-- {
		if c.scopes[i].funcBd {
			return i
		}
	}
	return -1
}

// lookup resolves a name to (slot, isUp, upIndex, found). Names in the
// current function resolve to locals; names reaching past the current
// function boundary resolve to this function's upvalues (captured by
// value at closure creation, indexed by their position in the closure's
// upvalue table).
func (c *Compiler) lookup(name string) (slot int, up bool, uidx int, found bool) {
	top := len(c.scopes) - 1
	base := c.fnBase()
	// Search the current function's scopes (innermost first).
	for i := top; i >= 0 && i >= base; i-- {
		if s, ok := c.scopes[i].locals[name]; ok {
			return s, false, 0, true
		}
	}
	// Beyond the function boundary: one of this function's upvalues. The
	// table lives on the function scope, which may sit below nested blocks.
	if base >= 0 {
		tsc := c.scopes[base]
		for ui, un := range tsc.ups {
			if un == name {
				return 0, true, ui, true
			}
		}
	}
	return 0, false, 0, false
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

func (c *Compiler) compileFn(d *fe.FnDecl, qual string) {
	c.b.MarkEntry(qual)
	c.funcEntry[qual] = d
	c.pushScope(true)
	c.funcName = qual
	c.nilFix = nil
	c.params[qual] = len(d.Args)
	for _, a := range d.Args {
		c.declare(a.Name)
	}
	if d.Body != nil {
		c.compileBlockValue(d.Body, true)
	} else {
		c.b.Emit(OpPushNil, 0, 0)
	}
	c.b.Emit(OpReturn, 0, 0)
	// `?` propagation epilogue.
	for _, at := range c.nilFix {
		c.b.Patch(at, c.b.Pos())
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	c.nilFix = nil
	c.popScope()
}

// compileLambda compiles a lambda to its own entry and leaves a closure.
func (c *Compiler) compileLambda(e *fe.LambdaExpr) {
	c.lambdaSeq++
	name := fmt.Sprintf("<lambda#%d>", c.lambdaSeq)
	// Free variables: names the body uses that resolve to enclosing-function
	// locals. They are captured by value at closure creation.
	free := c.freeVars(e)
	end := c.b.Emit(OpJump, 0, 0)
	// The lambda body resolves free names through its own upvalue table.
	c.b.MarkEntry(name)
	c.pushScope(true)
	c.funcName = name
	c.nilFix = nil
	c.params[name] = len(e.Params)
	base := c.scopes[len(c.scopes)-1]
	base.ups = append([]string{}, free...)
	for _, a := range e.Params {
		c.declare(a.Name)
	}
	if e.Body != nil {
		c.compileBlockValue(e.Body, true)
	} else if e.Single != nil {
		c.compileExpr(e.Single)
	} else {
		c.b.Emit(OpPushNil, 0, 0)
	}
	c.b.Emit(OpReturn, 0, 0)
	for _, at := range c.nilFix {
		c.b.Patch(at, c.b.Pos())
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	c.nilFix = nil
	c.popScope()
	c.b.Patch(end, c.b.Pos())
	// Capture in the enclosing frame, including its own captured values.
	for _, fv := range free {
		c.compileIdentLoad(fv, e.Span)
	}
	c.b.Emit(OpMakeClosure, int32(len(free)), c.pool.InternString(name))
}

// freeVars collects identifiers used in e that are declared in enclosing
// scopes ( Innermost-first, deduplicated).
func (c *Compiler) freeVars(e *fe.LambdaExpr) []string {
	declared := map[string]bool{}
	for _, a := range e.Params {
		declared[a.Name] = true
	}
	var used []string
	seen := map[string]bool{}
	v := &identCollector{onIdent: func(n string) {
		if !declared[n] && !seen[n] {
			if _, _, _, found := c.lookup(n); found {
				seen[n] = true
				used = append(used, n)
			}
		}
	}}
	if e.Body != nil {
		walkBlockIdents(v, e.Body)
	}
	if e.Single != nil {
		walkExprIdents(v, e.Single)
	}
	return used
}

type identCollector struct{ onIdent func(string) }

func walkBlockIdents(v *identCollector, b *fe.Block) {
	if b == nil {
		return
	}
	for _, s := range b.Stmts {
		walkStmtIdents(v, s)
	}
}

func walkStmtIdents(v *identCollector, s fe.Stmt) {
	switch s := s.(type) {
	case *fe.Block:
		walkBlockIdents(v, s)
	case *fe.VarDeclStmt:
		if s.Init != nil {
			walkExprIdents(v, s.Init)
		}
	case *fe.ExprStmt:
		walkExprIdents(v, s.X)
	case *fe.ReturnStmt:
		if s.Value != nil {
			walkExprIdents(v, s.Value)
		}
	case *fe.IfStmt:
		walkExprIdents(v, s.Cond)
		walkBlockIdents(v, s.Then)
		walkBlockIdents(v, s.Else)
	case *fe.WhileStmt:
		walkExprIdents(v, s.Cond)
		walkBlockIdents(v, s.Body)
	case *fe.ForStmt:
		walkExprIdents(v, s.Iter)
		walkBlockIdents(v, s.Body)
	case *fe.LoopStmt:
		walkBlockIdents(v, s.Body)
	case *fe.MatchStmt:
		walkExprIdents(v, s.X)
		for _, a := range s.Arms {
			if a.Guard != nil {
				walkExprIdents(v, a.Guard)
			}
			if a.Value != nil {
				walkExprIdents(v, a.Value)
			}
			walkBlockIdents(v, a.Body)
		}
	case *fe.ForEachStmt:
		walkExprIdents(v, s.Iter)
		walkBlockIdents(v, s.Body)
	case *fe.EmitStmt:
		walkExprIdents(v, s.Event)
	case *fe.DeferStmt:
		walkBlockIdents(v, s.Body)
	case *fe.UnsafeStmt:
		walkBlockIdents(v, s.Body)
	case *fe.BreakStmt:
		walkExprIdents(v, s.Value)
	}
}

func walkExprIdents(v *identCollector, e fe.Expr) {
	switch e := e.(type) {
	case *fe.IdentExpr:
		v.onIdent(e.Name)
	case *fe.UnaryExpr:
		walkExprIdents(v, e.X)
	case *fe.QuestionExpr:
		walkExprIdents(v, e.X)
	case *fe.BinaryExpr:
		walkExprIdents(v, e.X)
		walkExprIdents(v, e.Y)
	case *fe.AssignExpr:
		walkExprIdents(v, e.X)
		walkExprIdents(v, e.Y)
	case *fe.TernaryExpr:
		walkExprIdents(v, e.Cond)
		walkExprIdents(v, e.Then)
		walkExprIdents(v, e.Else)
	case *fe.CallExpr:
		walkExprIdents(v, e.Fn)
		for _, a := range e.Args {
			walkExprIdents(v, a.Value)
		}
	case *fe.MemberExpr:
		walkExprIdents(v, e.X)
	case *fe.IndexExpr:
		walkExprIdents(v, e.X)
		walkExprIdents(v, e.Index)
	case *fe.ArrayExpr:
		for _, x := range e.Elems {
			walkExprIdents(v, x)
		}
	case *fe.TupleExpr:
		for _, x := range e.Elems {
			walkExprIdents(v, x)
		}
	case *fe.ArrayRepeatExpr:
		walkExprIdents(v, e.Elem)
		walkExprIdents(v, e.Count)
	case *fe.StructExpr:
		for _, f := range e.Fields {
			if f.Value != nil {
				walkExprIdents(v, f.Value)
			} else {
				v.onIdent(f.Field)
			}
		}
	case *fe.LambdaExpr:
		if e.Body != nil {
			walkBlockIdents(v, e.Body)
		}
		if e.Single != nil {
			walkExprIdents(v, e.Single)
		}
	case *fe.IfExpr:
		walkExprIdents(v, e.Cond)
		walkBlockIdents(v, e.Then)
		walkBlockIdents(v, e.Else)
		walkExprIdents(v, e.ElseIf)
	case *fe.MatchExpr:
		walkExprIdents(v, e.X)
		for _, a := range e.Arms {
			walkExprIdents(v, a.Guard)
			if a.Value != nil {
				walkExprIdents(v, a.Value)
			}
			walkBlockIdents(v, a.Body)
		}
	}
}
