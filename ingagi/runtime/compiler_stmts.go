package runtime

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Statement lowering: AST statements to tape with jump patching. Expression
// statements leave no residue; blocks open lexical scopes; loops thread
// break/continue through loopCtx; ECS statements delegate iteration,
// events and renderer edges to dedicated opcodes executed by dispatch.go.

// compileBlockValue compiles a block; when wantValue the last expression
// statement's value stays on the stack, otherwise the stack is balanced.
func (c *Compiler) compileBlockValue(b *fe.Block, wantValue bool) {
	if b == nil {
		if wantValue {
			c.b.Emit(OpPushNil, 0, 0)
		}
		return
	}
	c.pushScope(false)
	last := len(b.Stmts) - 1
	for i, s := range b.Stmts {
		if wantValue && i == last {
			if es, ok := s.(*fe.ExprStmt); ok {
				c.compileExpr(es.X)
				continue
			}
		}
		c.compileStmt(s)
	}
	if wantValue && (last < 0 || !isExprStmt(b.Stmts[last])) {
		c.b.Emit(OpPushNil, 0, 0)
	}
	c.popScope()
}

func isExprStmt(s fe.Stmt) bool {
	_, ok := s.(*fe.ExprStmt)
	return ok
}

func (c *Compiler) compileStmt(s fe.Stmt) {
	switch s := s.(type) {
	case *fe.Block:
		c.compileBlockValue(s, false)
	case *fe.DeclStmt:
		c.compileDeclStmt(s)
	case *fe.VarDeclStmt:
		if s.Init != nil {
			c.compileExpr(s.Init)
		} else {
			c.b.Emit(OpPushNil, 0, 0)
		}
		slot := c.declare(s.Name)
		c.b.Emit(OpStoreLocal, int32(slot), 0)
	case *fe.ExprStmt:
		c.compileExpr(s.X)
		c.b.Emit(OpPop, 0, 0)
	case *fe.ReturnStmt:
		if s.Value != nil {
			c.compileExpr(s.Value)
		} else {
			c.b.Emit(OpPushNil, 0, 0)
		}
		c.b.Emit(OpReturn, 0, 0)
	case *fe.BreakStmt:
		c.compileBreak(s)
	case *fe.ContinueStmt:
		c.compileContinue()
	case *fe.IfStmt:
		c.compileIf(s.Cond, s.Then, s.Else, s.ElseIf, false)
	case *fe.WhileStmt:
		c.compileWhile(s)
	case *fe.ForStmt:
		c.compileFor(s)
	case *fe.LoopStmt:
		c.compileLoop(s)
	case *fe.MatchStmt:
		c.compileMatchStmt(s)
	case *fe.DeferStmt:
		c.compileDefer(s)
	case *fe.UnsafeStmt:
		c.compileBlockValue(s.Body, false)
	case *fe.ForEachStmt:
		c.compileForEach(s)
	case *fe.BindStmt:
		c.compileBind(s)
	case *fe.EmitStmt:
		c.compileExpr(s.Event)
		c.b.Emit(OpEmit, 0, 0)
	case *fe.OnStmt:
		c.compileOn(s)
	default:
		c.errf(s.Pos(), "unsupported statement %T", s)
		c.b.Emit(OpPushNil, 0, 0)
		c.b.Emit(OpPop, 0, 0)
	}
}

// compileDeclStmt handles item declarations inside blocks (extend bodies,
// local items): functions register entries; consts evaluate immediately.
func (c *Compiler) compileDeclStmt(s *fe.DeclStmt) {
	switch d := s.Decl.(type) {
	case *fe.FnDecl:
		end := c.b.Emit(OpJump, 0, 0)
		c.compileFn(d, c.funcName+"::"+d.Name)
		c.b.Patch(end, c.b.Pos())
	case *fe.ConstDecl:
		c.compileExpr(d.Value)
		slot := c.declare(d.Name)
		c.b.Emit(OpStoreLocal, int32(slot), 0)
	default:
		c.errf(s.Pos(), "declaration %T not supported in blocks", s.Decl)
	}
}

func (c *Compiler) compileBreak(s *fe.BreakStmt) {
	if len(c.loops) == 0 {
		c.errf(s.Pos(), "break outside loop")
		return
	}
	l := c.loops[len(c.loops)-1]
	if s.Value != nil {
		c.compileExpr(s.Value)
		c.b.Emit(OpStoreLocal, int32(l.result), 0)
	}
	l.breaks = append(l.breaks, c.b.Emit(OpJump, 0, 0))
}

func (c *Compiler) compileContinue() {
	if len(c.loops) == 0 {
		return
	}
	l := c.loops[len(c.loops)-1]
	l.continues = append(l.continues, c.b.Emit(OpJump, 0, 0))
}

func (c *Compiler) pushLoop(result int) *loopCtx {
	l := &loopCtx{result: result}
	c.loops = append(c.loops, l)
	return l
}

func (c *Compiler) popLoop() *loopCtx {
	l := c.loops[len(c.loops)-1]
	c.loops = c.loops[:len(c.loops)-1]
	return l
}

// compileIf lowers if/else chains. wantValue threads branch values.
func (c *Compiler) compileIf(cond fe.Expr, then, els *fe.Block, elsif fe.Stmt, wantValue bool) {
	c.compileExpr(cond)
	jElse := c.b.Emit(OpJumpIfFalse, 0, 0)
	c.compileBlockValue(then, wantValue)
	jEnd := c.b.Emit(OpJump, 0, 0)
	c.b.Patch(jElse, c.b.Pos())
	switch {
	case elsif != nil:
		if is, ok := elsif.(*fe.IfStmt); ok {
			c.compileIf(is.Cond, is.Then, is.Else, is.ElseIf, wantValue)
		} else if es, ok := elsif.(fe.Expr); ok {
			c.compileExpr(es)
		} else {
			c.compileStmt(elsif)
			if wantValue {
				c.b.Emit(OpPushNil, 0, 0)
			}
		}
	case els != nil:
		c.compileBlockValue(els, wantValue)
	default:
		if wantValue {
			c.b.Emit(OpPushNil, 0, 0)
		}
	}
	c.b.Patch(jEnd, c.b.Pos())
}

func (c *Compiler) compileWhile(s *fe.WhileStmt) {
	l := c.pushLoop(-1)
	top := c.b.Pos()
	c.compileExpr(s.Cond)
	jEnd := c.b.Emit(OpJumpIfFalse, 0, 0)
	c.compileBlockValue(s.Body, false)
	for _, at := range l.continues {
		c.b.Patch(at, top)
	}
	c.b.Emit(OpJump, int32(top), 0)
	c.b.Patch(jEnd, c.b.Pos())
	for _, at := range l.breaks {
		c.b.Patch(at, c.b.Pos())
	}
	c.popLoop()
}

func (c *Compiler) compileLoop(s *fe.LoopStmt) {
	res := c.declareHidden()
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpStoreLocal, int32(res), 0)
	l := c.pushLoop(res)
	top := c.b.Pos()
	c.compileBlockValue(s.Body, false)
	for _, at := range l.continues {
		c.b.Patch(at, top)
	}
	c.b.Emit(OpJump, int32(top), 0)
	end := c.b.Pos()
	for _, at := range l.breaks {
		c.b.Patch(at, end)
	}
	c.popLoop()
	c.b.Emit(OpLoadLocal, int32(res), 0)
	c.b.Emit(OpPop, 0, 0) // loop is a statement: discard value
}

// compileFor lowers `for pat in expr` over arrays (and strings).
func (c *Compiler) compileFor(s *fe.ForStmt) {
	iter := c.declareHidden()
	idx := c.declareHidden()
	l := c.pushLoop(-1)
	c.compileExpr(s.Iter)
	c.b.Emit(OpStoreLocal, int32(iter), 0)
	c.b.Emit(OpPushInt, 0, c.pool.InternInt(0))
	c.b.Emit(OpStoreLocal, int32(idx), 0)
	top := c.b.Pos()
	// idx < len(iter) ?
	c.b.Emit(OpLoadLocal, int32(idx), 0)
	c.b.Emit(OpLoadLocal, int32(iter), 0)
	c.b.Emit(OpLen, 0, 0)
	c.b.Emit(OpLt, 0, 0)
	jEnd := c.b.Emit(OpJumpIfFalse, 0, 0)
	// elem = iter[idx]
	c.b.Emit(OpLoadLocal, int32(iter), 0)
	c.b.Emit(OpLoadLocal, int32(idx), 0)
	c.b.Emit(OpLoadIndex, 0, 0)
	elem := c.declareHidden()
	c.b.Emit(OpStoreLocal, int32(elem), 0)
	// pattern bind with mismatch -> continue
	cont := c.compilePatternSlot(s.Pat, elem, nil)
	c.compileBlockValue(s.Body, false)
	step := c.b.Pos()
	for _, at := range cont {
		c.b.Patch(at, step)
	}
	// idx += 1
	c.b.Emit(OpLoadLocal, int32(idx), 0)
	c.b.Emit(OpPushInt, 0, c.pool.InternInt(1))
	c.b.Emit(OpAdd, 0, 0)
	c.b.Emit(OpStoreLocal, int32(idx), 0)
	for _, at := range l.continues {
		c.b.Patch(at, step)
	}
	c.b.Emit(OpJump, int32(top), 0)
	c.b.Patch(jEnd, c.b.Pos())
	for _, at := range l.breaks {
		c.b.Patch(at, c.b.Pos())
	}
	c.popLoop()
}

// compilePatternSlot matches a pattern against a slot's value. Mismatches
// jump to the returned fixups (caller patches to continue/next-arm).
func (c *Compiler) compilePatternSlot(pat fe.Pattern, subj int, _ []int) []int {
	var fails []int
	c.matchSlot(pat, subj, &fails)
	return fails
}

// matchSlot emits a test of slot subj; failures jump to *fails fixups.
func (c *Compiler) matchSlot(pat fe.Pattern, subj int, fails *[]int) {
	fail := func() {
		*fails = append(*fails, c.b.Emit(OpJumpIfFalse, 0, 0))
	}
	switch p := pat.(type) {
	case *fe.WildcardPat:
		// Always matches.
	case *fe.BindPat:
		c.b.Emit(OpLoadLocal, int32(subj), 0)
		slot := c.declare(p.Name)
		c.b.Emit(OpStoreLocal, int32(slot), 0)
	case *fe.LitPat:
		c.b.Emit(OpLoadLocal, int32(subj), 0)
		c.compileLit(p.Value)
		c.b.Emit(OpEq, 0, 0)
		fail()
	case *fe.PathPat:
		c.b.Emit(OpLoadLocal, int32(subj), 0)
		c.b.Emit(OpLoadGlobal, 0, c.pool.InternString(p.Path.String()))
		c.b.Emit(OpEq, 0, 0)
		fail()
	case *fe.TuplePat:
		c.b.Emit(OpLoadLocal, int32(subj), 0)
		c.b.Emit(OpLen, 0, 0)
		c.b.Emit(OpPushInt, 0, c.pool.InternInt(int64(len(p.Elems))))
		c.b.Emit(OpEq, 0, 0)
		fail()
		for i, e := range p.Elems {
			tmp := c.declareHidden()
			c.b.Emit(OpLoadLocal, int32(subj), 0)
			c.b.Emit(OpPushInt, 0, c.pool.InternInt(int64(i)))
			c.b.Emit(OpLoadIndex, 0, 0)
			c.b.Emit(OpStoreLocal, int32(tmp), 0)
			c.matchSlot(e, tmp, fails)
		}
	case *fe.StructPat:
		for _, f := range p.Fields {
			tmp := c.declareHidden()
			c.b.Emit(OpLoadLocal, int32(subj), 0)
			c.b.Emit(OpTryField, 0, c.pool.InternString(f.Field))
			c.b.Emit(OpStoreLocal, int32(tmp), 0)
			// A missing (nil) field fails the pattern.
			c.b.Emit(OpLoadLocal, int32(tmp), 0)
			c.b.Emit(OpPushNil, 0, 0)
			c.b.Emit(OpNe, 0, 0)
			fail()
			if f.Pat != nil {
				c.matchSlot(f.Pat, tmp, fails)
			} else {
				// Shorthand `Field` (no `: sub`) binds the field's own
				// name; `Field: name` binds `name` (handled above).
				slot := c.declare(f.Field)
				c.b.Emit(OpLoadLocal, int32(tmp), 0)
				c.b.Emit(OpStoreLocal, int32(slot), 0)
			}
		}
	case *fe.ArrayPat:
		c.b.Emit(OpLoadLocal, int32(subj), 0)
		c.b.Emit(OpLen, 0, 0)
		c.b.Emit(OpPushInt, 0, c.pool.InternInt(int64(len(p.Elems))))
		if p.HasRest {
			c.b.Emit(OpGe, 0, 0)
		} else {
			c.b.Emit(OpEq, 0, 0)
		}
		fail()
		for i, e := range p.Elems {
			tmp := c.declareHidden()
			c.b.Emit(OpLoadLocal, int32(subj), 0)
			c.b.Emit(OpPushInt, 0, c.pool.InternInt(int64(i)))
			c.b.Emit(OpLoadIndex, 0, 0)
			c.b.Emit(OpStoreLocal, int32(tmp), 0)
			c.matchSlot(e, tmp, fails)
		}
		if p.HasRest && p.Rest != "" {
			c.errf(p.Pos(), "array rest binding `..%s` is not supported in v1", p.Rest)
		}
	case *fe.OrPat:
		var joins []int
		for i, alt := range p.Alts {
			var altFail []int
			c.matchSlot(alt, subj, &altFail)
			if i == len(p.Alts)-1 {
				// Last alternative: failures propagate outward.
				*fails = append(*fails, altFail...)
				break
			}
			joins = append(joins, c.b.Emit(OpJump, 0, 0))
			next := c.b.Pos()
			for _, at := range altFail {
				c.b.Patch(at, next)
			}
		}
		join := c.b.Pos()
		for _, at := range joins {
			c.b.Patch(at, join)
		}
	default:
		c.errf(pat.Pos(), "unsupported pattern %T", pat)
	}
}

// compileMatchStmt lowers match statements; arms share one subject slot.
func (c *Compiler) compileMatchStmt(s *fe.MatchStmt) {
	c.compileExpr(s.X)
	subj := c.declareHidden()
	c.b.Emit(OpStoreLocal, int32(subj), 0)
	var ends []int
	for _, arm := range s.Arms {
		var fails []int
		c.matchSlot(arm.Pat, subj, &fails)
		if arm.Guard != nil {
			c.compileExpr(arm.Guard)
			fails = append(fails, c.b.Emit(OpJumpIfFalse, 0, 0))
		}
		if arm.Body != nil {
			c.compileBlockValue(arm.Body, false)
		} else if arm.Value != nil {
			c.compileExpr(arm.Value)
			c.b.Emit(OpPop, 0, 0)
		}
		ends = append(ends, c.b.Emit(OpJump, 0, 0))
		next := c.b.Pos()
		for _, at := range fails {
			c.b.Patch(at, next)
		}
	}
	end := c.b.Pos()
	for _, at := range ends {
		c.b.Patch(at, end)
	}
}

// compileDefer registers a deferred block running at scope return.
func (c *Compiler) compileDefer(s *fe.DeferStmt) {
	reg := c.b.Emit(OpDefer, 0, 0)
	jEnd := c.b.Emit(OpJump, 0, 0)
	body := c.b.Pos()
	c.b.Patch(reg, body)
	c.compileBlockValue(s.Body, false)
	c.b.Emit(OpEndDefer, 0, 0)
	c.b.Patch(jEnd, c.b.Pos())
}

// compileForEach lowers `for each(bindings) in iter`: the iterator carries
// per-row component snapshots; &mut bindings write back on advance/exit.
// Alias slots are declared up front so the iterator can fill them directly.
func (c *Compiler) compileForEach(s *fe.ForEachStmt) {
	slots := make([]int32, len(s.Bindings))
	aliases := make([]string, len(s.Bindings))
	for i, bd := range s.Bindings {
		alias := bd.Alias
		if alias == "" {
			alias = bd.Name
		}
		aliases[i] = alias
		slots[i] = int32(c.declare(alias))
	}
	c.compileExpr(s.Iter)
	for i, bd := range s.Bindings {
		c.b.Emit(OpPushString, 0, c.pool.InternString(bd.Name))
		c.b.Emit(OpPushBool, boolToI(bd.Mut), 0)
		c.b.Emit(OpPushString, 0, c.pool.InternString(aliases[i]))
		c.b.Emit(OpPushInt, 0, c.pool.InternInt(int64(slots[i])))
	}
	c.b.Emit(OpForEachBegin, int32(len(s.Bindings)), 0)
	l := c.pushLoop(-1)
	top := c.b.Pos()
	jEnd := c.b.Emit(OpForEachNext, 0, 0)
	c.compileBlockValue(s.Body, false)
	for _, at := range l.continues {
		c.b.Patch(at, top)
	}
	c.b.Emit(OpJump, int32(top), 0)
	end := c.b.Pos()
	c.b.Patch(jEnd, end)
	for _, at := range l.breaks {
		c.b.Patch(at, end)
	}
	c.popLoop()
	c.b.Emit(OpForEachEnd, 0, 0)
}

// compileBind records a renderer edge: pipeline x stage over a query.
func (c *Compiler) compileBind(s *fe.BindStmt) {
	c.compileExpr(s.Iter)
	c.b.Emit(OpPushString, 0, c.pool.InternString(s.Pipeline))
	c.b.Emit(OpPushString, 0, c.pool.InternString(s.Stage))
	for _, q := range s.With {
		c.b.Emit(OpPushString, 0, c.pool.InternString(q.Type.Last()))
	}
	for _, q := range s.Without {
		c.b.Emit(OpPushString, 0, c.pool.InternString(q.Type.Last()))
	}
	c.b.EmitB(OpBind, int32(len(s.With)), int32(len(s.Without)), 0)
}

// compileOn registers an inline observer: body becomes a closure taking
// the component snapshot, dispatched on add/remove/set.
func (c *Compiler) compileOn(s *fe.OnStmt) {
	// Build a synthetic zero-param lambda around the body.
	c.lambdaSeq++
	name := itoa(int64(c.lambdaSeq)) + "$on"
	end := c.b.Emit(OpJump, 0, 0)
	c.b.MarkEntry("<on#" + name + ">")
	c.pushScope(true)
	c.funcName = "<on#" + name + ">"
	c.params["<on#"+name+">"] = 1 // snapshot argument
	// Slot 0 receives the component snapshot argument at dispatch time.
	c.declare("it")
	c.compileBlockValue(s.Body, false)
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	for _, at := range c.nilFix {
		c.b.Patch(at, c.b.Pos())
	}
	c.b.Emit(OpPushNil, 0, 0)
	c.b.Emit(OpReturn, 0, 0)
	c.popScope()
	c.b.Patch(end, c.b.Pos())
	c.b.Emit(OpMakeClosure, 0, c.pool.InternString("<on#"+name+">"))
	kind := int32(0)
	switch s.Kind {
	case "remove":
		kind = 1
	case "set":
		kind = 2
	}
	c.b.Emit(OpObserve, kind, c.pool.InternString(s.Comp.Last()))
}

func boolToI(b bool) int32 {
	if b {
		return 1
	}
	return 0
}
