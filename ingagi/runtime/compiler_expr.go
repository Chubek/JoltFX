package runtime

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Expression lowering. Every expression leaves exactly one value on the
// stack. Name resolution order: locals, upvalues, globals, functions
// (as closures), builtins — so scripts stay dynamically relinkable and
// hot reload can swap function bodies under live references.

func (c *Compiler) compileExpr(e fe.Expr) {
	switch e := e.(type) {
	case *fe.Lit:
		c.compileLit(e)
	case *fe.IdentExpr:
		c.compileIdentLoad(e.Name, e.Span)
	case *fe.PathExpr:
		c.b.Emit(OpLoadGlobal, 0, c.pool.InternString(e.Path.String()))
	case *fe.SelfExpr:
		c.b.Emit(OpLoadLocal, 0, 0)
	case *fe.UnaryExpr:
		c.compileUnary(e)
	case *fe.BinaryExpr:
		c.compileBinary(e)
	case *fe.AssignExpr:
		c.compileAssign(e)
	case *fe.TernaryExpr:
		c.compileExpr(e.Cond)
		jElse := c.b.Emit(OpJumpIfFalse, 0, 0)
		c.compileExpr(e.Then)
		jEnd := c.b.Emit(OpJump, 0, 0)
		c.b.Patch(jElse, c.b.Pos())
		c.compileExpr(e.Else)
		c.b.Patch(jEnd, c.b.Pos())
	case *fe.CallExpr:
		c.compileCall(e)
	case *fe.MemberExpr:
		c.compileExpr(e.X)
		c.b.Emit(OpLoadField, 0, c.pool.InternString(e.Field))
	case *fe.IndexExpr:
		c.compileExpr(e.X)
		c.compileExpr(e.Index)
		c.b.Emit(OpLoadIndex, 0, 0)
	case *fe.QuestionExpr:
		// Postfix `?` only needs the nil check when the call can actually
		// produce nil, which the VM decides at run time.
		c.compileExpr(e.X)
		c.nilFix = append(c.nilFix, c.b.Emit(OpJumpIfNil, 0, 0))
	case *fe.TupleExpr:
		for _, x := range e.Elems {
			c.compileExpr(x)
		}
		c.b.Emit(OpMakeArray, int32(len(e.Elems)), 0)
	case *fe.ArrayExpr:
		for _, x := range e.Elems {
			c.compileExpr(x)
		}
		c.b.Emit(OpMakeArray, int32(len(e.Elems)), 0)
	case *fe.ArrayRepeatExpr:
		c.compileExpr(e.Elem)
		c.compileExpr(e.Count)
		c.b.Emit(OpMakeArrayRepeat, 0, 0)
	case *fe.StructExpr:
		tn := e.Type.Last()
		for _, f := range e.Fields {
			c.b.Emit(OpPushString, 0, c.pool.InternString(f.Field))
			if f.Value != nil {
				c.compileExpr(f.Value)
			} else {
				c.compileIdentLoad(f.Field, f.Span)
			}
		}
		c.b.Emit(OpMakeStruct, int32(len(e.Fields)), c.pool.InternString(tn))
	case *fe.LambdaExpr:
		c.compileLambda(e)
	case *fe.IfExpr:
		c.compileExpr(e.Cond)
		jElse := c.b.Emit(OpJumpIfFalse, 0, 0)
		c.compileBlockValue(e.Then, true)
		jEnd := c.b.Emit(OpJump, 0, 0)
		c.b.Patch(jElse, c.b.Pos())
		switch {
		case e.ElseIf != nil:
			c.compileExpr(e.ElseIf)
		case e.Else != nil:
			c.compileBlockValue(e.Else, true)
		default:
			c.b.Emit(OpPushNil, 0, 0)
		}
		c.b.Patch(jEnd, c.b.Pos())
	case *fe.MatchExpr:
		c.compileMatchExpr(e)
	default:
		c.errf(e.Pos(), "unsupported expression %T", e)
		c.b.Emit(OpPushNil, 0, 0)
	}
}

func (c *Compiler) compileLit(l *fe.Lit) {
	switch l.Kind {
	case fe.LitNull:
		c.b.Emit(OpPushNil, 0, 0)
	case fe.LitBool:
		c.b.Emit(OpPushBool, boolToI(l.Bool), 0)
	case fe.LitInt:
		c.b.Emit(OpPushInt, 0, c.pool.InternInt(l.Int))
	case fe.LitFloat:
		c.b.Emit(OpPushFloat, 0, c.pool.InternFloat(l.Float))
	case fe.LitString:
		c.b.Emit(OpPushString, 0, c.pool.InternString(l.Str))
	case fe.LitChar:
		c.b.Emit(OpPushInt, 0, c.pool.InternInt(l.Int))
	}
}

func (c *Compiler) compileIdentLoad(name string, sp fe.Span) {
	if slot, up, ui, found := c.lookup(name); found {
		if up {
			c.b.Emit(OpLoadUp, int32(ui), 0)
		} else {
			c.b.Emit(OpLoadLocal, int32(slot), 0)
		}
		return
	}
	_ = sp
	c.b.Emit(OpLoadGlobal, 0, c.pool.InternString(name))
}

func (c *Compiler) compileUnary(e *fe.UnaryExpr) {
	switch e.Op {
	case "not":
		c.compileExpr(e.X)
		c.b.Emit(OpNot, 0, 0)
	case "!":
		c.compileExpr(e.X)
		c.b.Emit(OpNot, 0, 0)
	case "-":
		c.compileExpr(e.X)
		c.b.Emit(OpNeg, 0, 0)
	case "+":
		c.compileExpr(e.X)
	case "~":
		// Bitwise not via x ^ -1 (ints only, checked at runtime).
		c.compileExpr(e.X)
		c.b.Emit(OpPushInt, 0, c.pool.InternInt(-1))
		c.b.Emit(OpBitXor, 0, 0)
	case "await":
		// v1 runs async functions synchronously; await is a passthrough.
		c.compileExpr(e.X)
	case "*", "&":
		c.errf(e.Pos(), "pointer operators are not supported in v1")
		c.compileExpr(e.X)
	default:
		c.errf(e.Pos(), "unknown prefix %q", e.Op)
		c.compileExpr(e.X)
	}
}

func (c *Compiler) compileBinary(e *fe.BinaryExpr) {
	switch e.Op {
	case "&&":
		c.compileExpr(e.X)
		jFalse := c.b.Emit(OpJumpIfFalse, 0, 0)
		c.compileExpr(e.Y)
		jEnd := c.b.Emit(OpJump, 0, 0)
		c.b.Patch(jFalse, c.b.Pos())
		c.b.Emit(OpPushBool, 0, 0)
		c.b.Patch(jEnd, c.b.Pos())
	case "||":
		c.compileExpr(e.X)
		jTrue := c.b.Emit(OpJumpIfTrue, 0, 0)
		c.compileExpr(e.Y)
		jEnd := c.b.Emit(OpJump, 0, 0)
		c.b.Patch(jTrue, c.b.Pos())
		c.b.Emit(OpPushBool, 1, 0)
		c.b.Patch(jEnd, c.b.Pos())
	default:
		c.compileExpr(e.X)
		c.compileExpr(e.Y)
		switch e.Op {
		case "+":
			c.b.Emit(OpAdd, 0, 0)
		case "-":
			c.b.Emit(OpSub, 0, 0)
		case "*":
			c.b.Emit(OpMul, 0, 0)
		case "/":
			c.b.Emit(OpDiv, 0, 0)
		case "%":
			c.b.Emit(OpMod, 0, 0)
		case "==":
			c.b.Emit(OpEq, 0, 0)
		case "!=":
			c.b.Emit(OpNe, 0, 0)
		case "<":
			c.b.Emit(OpLt, 0, 0)
		case "<=":
			c.b.Emit(OpLe, 0, 0)
		case ">":
			c.b.Emit(OpGt, 0, 0)
		case ">=":
			c.b.Emit(OpGe, 0, 0)
		case "&":
			c.b.Emit(OpBitAnd, 0, 0)
		case "|":
			c.b.Emit(OpBitOr, 0, 0)
		case "^":
			c.b.Emit(OpBitXor, 0, 0)
		case "<<":
			c.b.Emit(OpShl, 0, 0)
		case ">>":
			c.b.Emit(OpShr, 0, 0)
		default:
			c.errf(e.Pos(), "unknown operator %q", e.Op)
			c.b.Emit(OpPop, 0, 0)
			c.b.Emit(OpPop, 0, 0)
			c.b.Emit(OpPushNil, 0, 0)
		}
	}
}

// compileAssign stores to ident/member/index targets; the value stays.
func (c *Compiler) compileAssign(e *fe.AssignExpr) {
	if e.Op != "=" {
		// Op-assign desugars to x = x op y (member/index evaluated twice;
		// side-effect-free targets only in v1 — documented).
		c.compileExpr(e.X)
		c.compileExpr(e.Y)
		switch e.Op {
		case "+=":
			c.b.Emit(OpAdd, 0, 0)
		case "-=":
			c.b.Emit(OpSub, 0, 0)
		case "*=":
			c.b.Emit(OpMul, 0, 0)
		case "/=":
			c.b.Emit(OpDiv, 0, 0)
		case "%=":
			c.b.Emit(OpMod, 0, 0)
		case "&=":
			c.b.Emit(OpBitAnd, 0, 0)
		case "|=":
			c.b.Emit(OpBitOr, 0, 0)
		case "^=":
			c.b.Emit(OpBitXor, 0, 0)
		case "<<=":
			c.b.Emit(OpShl, 0, 0)
		case ">>=":
			c.b.Emit(OpShr, 0, 0)
		default:
			c.errf(e.Pos(), "unknown assignment %q", e.Op)
		}
		c.b.Emit(OpDup, 0, 0)
		c.storeTarget(e.X)
		return
	}
	c.compileExpr(e.Y)
	c.b.Emit(OpDup, 0, 0)
	c.storeTarget(e.X)
}

// storeTarget pops the top into an assignment target.
func (c *Compiler) storeTarget(x fe.Expr) {
	switch x := x.(type) {
	case *fe.IdentExpr:
		if slot, up, ui, found := c.lookup(x.Name); found {
			if up {
				c.b.Emit(OpStoreUp, int32(ui), 0)
			} else {
				c.b.Emit(OpStoreLocal, int32(slot), 0)
			}
			return
		}
		c.b.Emit(OpStoreGlobal, 0, c.pool.InternString(x.Name))
	case *fe.MemberExpr:
		// Stack: [value]; need [obj, value]: spill value, load obj.
		tmp := c.declareHidden()
		c.b.Emit(OpStoreLocal, int32(tmp), 0)
		c.compileExpr(x.X)
		c.b.Emit(OpLoadLocal, int32(tmp), 0)
		c.b.Emit(OpStoreField, 0, c.pool.InternString(x.Field))
	case *fe.IndexExpr:
		tmp := c.declareHidden()
		c.b.Emit(OpStoreLocal, int32(tmp), 0)
		c.compileExpr(x.X)
		c.compileExpr(x.Index)
		c.b.Emit(OpLoadLocal, int32(tmp), 0)
		c.b.Emit(OpStoreIndex, 0, 0)
		c.b.Emit(OpLoadLocal, int32(tmp), 0) // leave assigned value
	default:
		c.errf(x.Pos(), "cannot assign to %T", x)
		c.b.Emit(OpPop, 0, 0)
	}
}

// compileCall lowers calls: named, method, and first-class callee forms.
func (c *Compiler) compileCall(e *fe.CallExpr) {
	// Method call `obj.method(args)`.
	if m, ok := e.Fn.(*fe.MemberExpr); ok && !m.Arrow {
		c.compileExpr(m.X)
		for _, a := range e.Args {
			c.compileExpr(a.Value)
		}
		c.b.Emit(OpCallMethod, int32(len(e.Args)), c.pool.InternString(m.Field))
		return
	}
	// Named call `foo(args)` / `a::b(args)`. A callee that resolves to a
	// local or upvalue is an indirect call through the slot (closures,
	// first-class functions); otherwise the name links at runtime.
	name := ""
	indirect := false
	switch fn := e.Fn.(type) {
	case *fe.IdentExpr:
		if slot, up, ui, found := c.lookup(fn.Name); found {
			if up {
				c.b.Emit(OpLoadUp, int32(ui), 0)
			} else {
				c.b.Emit(OpLoadLocal, int32(slot), 0)
			}
			indirect = true
		} else {
			name = fn.Name
		}
	case *fe.PathExpr:
		name = fn.Path.String()
	}
	if name != "" {
		for _, a := range e.Args {
			c.compileExpr(a.Value)
		}
		c.b.Emit(OpCall, int32(len(e.Args)), c.pool.InternString(name))
		return
	}
	if !indirect {
		// First-class callee.
		c.compileExpr(e.Fn)
	}
	for _, a := range e.Args {
		c.compileExpr(a.Value)
	}
	c.b.Emit(OpCall, int32(len(e.Args)), c.pool.InternString(""))
}

// compileMatchExpr lowers match as an expression (arms leave values).
func (c *Compiler) compileMatchExpr(e *fe.MatchExpr) {
	c.compileExpr(e.X)
	subj := c.declareHidden()
	c.b.Emit(OpStoreLocal, int32(subj), 0)
	var ends []int
	armed := false
	for _, arm := range e.Arms {
		var fails []int
		c.matchSlot(arm.Pat, subj, &fails)
		if arm.Guard != nil {
			c.compileExpr(arm.Guard)
			fails = append(fails, c.b.Emit(OpJumpIfFalse, 0, 0))
		}
		if arm.Body != nil {
			c.compileBlockValue(arm.Body, true)
		} else if arm.Value != nil {
			c.compileExpr(arm.Value)
		} else {
			c.b.Emit(OpPushNil, 0, 0)
		}
		armed = true
		ends = append(ends, c.b.Emit(OpJump, 0, 0))
		next := c.b.Pos()
		for _, at := range fails {
			c.b.Patch(at, next)
		}
	}
	if !armed {
		c.b.Emit(OpPushNil, 0, 0)
	}
	end := c.b.Pos()
	for _, at := range ends {
		c.b.Patch(at, end)
	}
}
