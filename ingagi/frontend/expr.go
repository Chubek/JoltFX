package frontend

// Expression parsing (ng: expr … postfix_suffix, primary_expr, arg, literal).
//
// Operator precedence mirrors the %left/%right tower in ingagi.ng: a Pratt
// loop over binary levels with longest-match terminal selection (so `<<=`
// wins over `<<`, `==` over `=`). Prefix operators (! ~ - + * & await) nest
// outside postfix; ternary and assignment are right-associative.

var assignOps = []string{"<<=", ">>=", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "="}

// parseExpr implements expr.
func (p *Parser) parseExpr() Expr { return p.parseAssign() }

// parseAssign implements assign_expr.
func (p *Parser) parseAssign() Expr {
	pos, line, col := p.mark()
	lhs := p.parseTernary()
	for _, op := range assignOps {
		if !p.peekOp(op) {
			continue
		}
		p.lit(op)
		rhs := p.parseAssign()
		return &AssignExpr{Op: op, X: lhs, Y: rhs, Span: p.spanFrom(pos, line, col)}
	}
	return lhs
}

// parseTernary implements ternary_expr.
func (p *Parser) parseTernary() Expr {
	pos, line, col := p.mark()
	cond := p.parseOr()
	if p.lit("?") {
		then := p.parseExpr()
		if !p.lit(":") {
			p.fail("expected `:` in ternary", p.spanAt(p.pos, p.pos))
			return cond
		}
		els := p.parseExpr()
		return &TernaryExpr{Cond: cond, Then: then, Else: els, Span: p.spanFrom(pos, line, col)}
	}
	return cond
}

type binLevel struct {
	ops  []string
	next func(*Parser) Expr
}

func (p *Parser) parseBinaryLevel(ops []string, next func(*Parser) Expr) Expr {
	pos, line, col := p.mark()
	lhs := next(p)
	for {
		matched := ""
		for _, op := range ops {
			if p.peekOp(op) {
				matched = op
				break
			}
		}
		if matched == "" {
			return lhs
		}
		p.lit(matched)
		rhs := next(p)
		lhs = &BinaryExpr{Op: matched, X: lhs, Y: rhs, Span: p.spanFrom(pos, line, col)}
	}
}

// parseOr implements or_expr; ... and_expr implements and_expr, etc.
func (p *Parser) parseOr() Expr {
	return p.parseBinaryLevel([]string{"||"}, (*Parser).parseAnd)
}
func (p *Parser) parseAnd() Expr {
	return p.parseBinaryLevel([]string{"&&"}, (*Parser).parseBitor)
}
func (p *Parser) parseBitor() Expr {
	return p.parseBinaryLevel([]string{"|"}, (*Parser).parseBitxor)
}
func (p *Parser) parseBitxor() Expr {
	return p.parseBinaryLevel([]string{"^"}, (*Parser).parseBitand)
}
func (p *Parser) parseBitand() Expr {
	return p.parseBinaryLevel([]string{"&"}, (*Parser).parseEq)
}
func (p *Parser) parseEq() Expr {
	return p.parseBinaryLevel([]string{"==", "!="}, (*Parser).parseRel)
}
func (p *Parser) parseRel() Expr {
	return p.parseBinaryLevel([]string{"<=", ">=", "<", ">"}, (*Parser).parseShift)
}
func (p *Parser) parseShift() Expr {
	return p.parseBinaryLevel([]string{"<<", ">>"}, (*Parser).parseAdd)
}
func (p *Parser) parseAdd() Expr {
	return p.parseBinaryLevel([]string{"+", "-"}, (*Parser).parseMul)
}
func (p *Parser) parseMul() Expr {
	return p.parseBinaryLevel([]string{"*", "/", "%"}, (*Parser).parseUnary)
}

// parseUnary implements unary_expr.
func (p *Parser) parseUnary() Expr {
	pos, line, col := p.mark()
	if op, ok := p.oneOf("!", "~", "-", "+", "*", "&"); ok {
		return &UnaryExpr{Op: op, X: p.parseUnary(), Span: p.spanFrom(pos, line, col)}
	}
	if w, ok := p.peekIdent(); ok && w == "await" {
		p.parseIdent()
		return &UnaryExpr{Op: "await", X: p.parseUnary(), Span: p.spanFrom(pos, line, col)}
	}
	return p.parsePostfix()
}

// parsePostfix implements postfix_expr + postfix_suffix.
func (p *Parser) parsePostfix() Expr {
	pos, line, col := p.mark()
	x := p.parsePrimary()
	for {
		switch {
		case p.peekLit("("):
			p.lit("(")
			var args []CallArg
			if !p.peekLit(")") {
				for {
					args = append(args, p.parseArg())
					if _, ok := p.oneOf(","); !ok {
						break
					}
				}
			}
			if !p.lit(")") {
				p.fail("expected `)`", p.spanAt(p.pos, p.pos))
			}
			x = &CallExpr{Fn: x, Args: args, Span: p.spanFrom(pos, line, col)}
		case p.peekLit(".") || p.peekLit("->"):
			arrow := p.lit("->")
			if !arrow {
				p.lit(".")
			}
			f := p.expectIdent("field name")
			x = &MemberExpr{X: x, Field: f.Name, Arrow: arrow, Span: p.spanFrom(pos, line, col)}
		case p.peekLit("["):
			p.lit("[")
			idx := p.parseExpr()
			if !p.lit("]") {
				p.fail("expected `]`", p.spanAt(p.pos, p.pos))
			}
			x = &IndexExpr{X: x, Index: idx, Span: p.spanFrom(pos, line, col)}
		case p.peekLit("::"):
			// GenericCallSuffix `::<T, ...>`. A `::` not followed by `<`
			// ends postfix parsing (e.g. paths already consumed whole).
			p.lit("::")
			if !p.lit("<") {
				p.fail("expected generic arguments", p.spanAt(p.pos, p.pos))
				return x
			}
			// Represented as a call with the type list folded into span; the
			// callee keeps its span via Merge in later passes.
			for {
				p.parseType()
				if _, ok := p.oneOf(","); !ok {
					break
				}
			}
			if !p.lit(">") {
				p.fail("expected `>`", p.spanAt(p.pos, p.pos))
			}
		case p.lit("?"):
			x = &QuestionExpr{X: x, Span: p.spanFrom(pos, line, col)}
		default:
			return x
		}
	}
}

// parseArg implements arg.
func (p *Parser) parseArg() CallArg {
	pos, line, col := p.mark()
	if name, ok := p.peekIdent(); ok && !isKeyword(name) {
		save := p.pos
		sline, scol := p.line, p.col
		p.parseIdent()
		if p.lit(":") {
			v := p.parseExpr()
			return CallArg{Name: name, Value: v, Span: p.spanFrom(pos, line, col)}
		}
		p.pos, p.line, p.col = save, sline, scol
	}
	v := p.parseExpr()
	return CallArg{Value: v, Span: v.Pos()}
}

// parsePrimary implements primary_expr.
func (p *Parser) parsePrimary() Expr {
	pos, line, col := p.mark()
	sp := func() Span { return p.spanFrom(pos, line, col) }
	_ = sp
	// if-expression
	if w, ok := p.peekIdent(); ok && w == "if" {
		return p.parseIfExpr()
	}
	if w, ok := p.peekIdent(); ok && w == "match" {
		return p.parseMatchExpr()
	}
	// lambda: `| ... | ...` but not `|| ...`
	if p.peekLit("|") && !p.peekLit("||") {
		return p.parseLambda()
	}
	// self
	if w, ok := p.peekIdent(); ok && w == "self" {
		p.parseIdent()
		return &SelfExpr{Span: p.spanFrom(pos, line, col)}
	}
	// parenthesized / tuple
	if p.peekLit("(") {
		p.lit("(")
		if p.peekLit(")") {
			p.lit(")")
			return &TupleExpr{Span: p.spanFrom(pos, line, col)}
		}
		first := p.parseExpr()
		if p.lit(",") {
			elems := []Expr{first}
			if !p.peekLit(")") {
				for {
					// tuple tail may hold named args like struct inits
					elems = append(elems, p.parseExpr())
					if _, ok := p.oneOf(","); !ok {
						break
					}
					if p.peekLit(")") {
						break
					}
				}
			}
			if !p.lit(")") {
				p.fail("expected `)`", p.spanAt(p.pos, p.pos))
			}
			return &TupleExpr{Elems: elems, Span: p.spanFrom(pos, line, col)}
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
		return first
	}
	// array / repeat
	if p.peekLit("[") {
		p.lit("[")
		if p.peekLit("]") {
			p.lit("]")
			return &ArrayExpr{Span: p.spanFrom(pos, line, col)}
		}
		first := p.parseExpr()
		if p.lit(";") {
			count := p.parseExpr()
			if !p.lit("]") {
				p.fail("expected `]`", p.spanAt(p.pos, p.pos))
			}
			return &ArrayRepeatExpr{Elem: first, Count: count, Span: p.spanFrom(pos, line, col)}
		}
		elems := []Expr{first}
		for p.lit(",") {
			if p.peekLit("]") {
				break
			}
			elems = append(elems, p.parseExpr())
		}
		if !p.lit("]") {
			p.fail("expected `]`", p.spanAt(p.pos, p.pos))
		}
		return &ArrayExpr{Elems: elems, Span: p.spanFrom(pos, line, col)}
	}
	// qualified name or struct literal or literal or ident.
	// `Path {` is a struct literal only per tryStructBrace; otherwise the
	// brace belongs to control flow (`match x {`, `in world {`).
	if q, ok := p.peekPath(); ok {
		p.parsePath()
		if p.tryStructBrace(q.Last()) {
			p.lit("{")
			var inits []StructInit
			if !p.peekLit("}") {
				for {
					inits = append(inits, p.parseStructInit())
					if _, ok := p.oneOf(","); !ok {
						break
					}
					if p.peekLit("}") {
						break
					}
				}
			}
			if !p.lit("}") {
				p.fail("expected `}`", p.spanAt(p.pos, p.pos))
			}
			return &StructExpr{Type: q, Fields: inits, Span: p.spanFrom(pos, line, col)}
		}
		if len(q.Parts) == 1 && !q.Global {
			return &IdentExpr{Name: q.Parts[0].Name, Span: p.spanFrom(pos, line, col)}
		}
		return &PathExpr{Path: q, Span: p.spanFrom(pos, line, col)}
	}
	// literal (numbers, strings, booleans, null)
	p.skipTrivia()
	if !p.eof() {
		c := p.src[p.pos]
		if (c >= '0' && c <= '9') || c == '"' || c == '\'' {
			return p.parseLit()
		}
	}
	if w, ok := p.peekIdent(); ok && (w == "true" || w == "false" || w == "null") {
		return p.parseLit()
	}
	p.fail("expected expression", p.spanAt(p.pos, p.pos))
	// Recovery value keeps the tree walkable.
	return &Lit{Kind: LitNull, Span: p.spanAt(p.pos, p.pos)}
}

// peekPath reports whether a qualified_name starts at the cursor.
func (p *Parser) peekPath() (Path, bool) {
	pos, line, col := p.mark()
	q, ok := p.parsePath()
	p.pos, p.line, p.col = pos, line, col
	if !ok {
		return Path{}, false
	}
	if len(q.Parts) == 1 && isKeyword(q.Parts[0].Name) {
		return Path{}, false
	}
	return q, true
}

// parseStructInit implements struct_init.
func (p *Parser) parseStructInit() StructInit {
	pos, line, col := p.mark()
	name := p.expectIdent("field name")
	var v Expr
	if p.lit(":") {
		v = p.parseExpr()
	}
	return StructInit{Field: name.Name, Value: v, Span: p.spanFrom(pos, line, col)}
}

// parseLambda implements lambda_expr.
func (p *Parser) parseLambda() Expr {
	pos, line, col := p.mark()
	p.lit("|")
	var params []Param
	if !p.peekLit("|") {
		for {
			params = append(params, p.parseParam())
			if _, ok := p.oneOf(","); !ok {
				break
			}
		}
	}
	if !p.lit("|") {
		p.fail("expected `|`", p.spanAt(p.pos, p.pos))
	}
	var ret Type
	if p.lit("->") {
		ret = p.parseType()
	}
	if p.peekLit("{") {
		return &LambdaExpr{Params: params, Ret: ret, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
	}
	return &LambdaExpr{Params: params, Ret: ret, Single: p.parseExpr(), Span: p.spanFrom(pos, line, col)}
}

// parseIfExpr implements if_expr.
func (p *Parser) parseIfExpr() Expr {
	pos, line, col := p.mark()
	p.lit("if")
	cond := p.parseExpr()
	then := p.parseBlock()
	var els *Block
	var elsif Expr
	if w, ok := p.peekIdent(); ok && w == "else" {
		p.parseIdent()
		if w2, ok2 := p.peekIdent(); ok2 && w2 == "if" {
			elsif = p.parseIfExpr()
		} else {
			els = p.parseBlock()
		}
	}
	return &IfExpr{Cond: cond, Then: then, Else: els, ElseIf: elsif, Span: p.spanFrom(pos, line, col)}
}

// parseMatchExpr implements the expression form of match.
func (p *Parser) parseMatchExpr() Expr {
	pos, line, col := p.mark()
	p.lit("match")
	x := p.parseExpr()
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	arms := p.parseMatchArms()
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	return &MatchExpr{X: x, Arms: arms, Span: p.spanFrom(pos, line, col)}
}

// parseMatchArms implements match_arms.
func (p *Parser) parseMatchArms() []MatchArm {
	var out []MatchArm
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			return out
		}
		pos, line, col := p.mark()
		pat := p.parsePattern()
		var guard Expr
		if w, ok := p.peekIdent(); ok && w == "if" {
			p.parseIdent()
			guard = p.parseExpr()
		}
		if !p.lit("=>") {
			p.fail("expected `=>`", p.spanAt(p.pos, p.pos))
			return out
		}
		var body *Block
		var value Expr
		if p.peekLit("{") {
			body = p.parseBlock()
		} else {
			value = p.parseExpr()
		}
		out = append(out, MatchArm{Pat: pat, Guard: guard, Body: body, Value: value, Span: p.spanFrom(pos, line, col)})
		if _, ok := p.oneOf(","); !ok {
			return out
		}
	}
}

// parsePattern implements pattern.
func (p *Parser) parsePattern() Pattern {
	pos, line, col := p.mark()
	sp := func() Span { return p.spanFrom(pos, line, col) }
	// tuple / struct patterns need lookahead; try struct first via path+`{`
	if q, ok := p.peekPath(); ok {
		// Could be BindPat (single ident) — but a lone ident followed by `{`
		// is a struct pattern. Peek further.
		p.parsePath()
		if p.peekLit("{") {
			p.lit("{")
			var fields []StructFieldPat
			if !p.peekLit("}") {
				for {
					fpos, fline, fcol := p.mark()
					name := p.expectIdent("field pattern")
					var sub Pattern
					if p.lit(":") {
						sub = p.parsePattern()
					} else {
						// Shorthand `Field` binds the field name itself, so
						// the compiler sees an explicit binding pattern.
						sub = &BindPat{Name: name.Name, Span: p.spanFrom(fpos, fline, fcol)}
					}
					fields = append(fields, StructFieldPat{Field: name.Name, Pat: sub, Span: p.spanFrom(fpos, fline, fcol)})
					if _, ok := p.oneOf(","); !ok {
						break
					}
					if p.peekLit("}") {
						break
					}
				}
			}
			if !p.lit("}") {
				p.fail("expected `}`", p.spanAt(p.pos, p.pos))
			}
			base := Pattern(&StructPat{Type: q, Fields: fields, Span: sp()})
			return p.parseOrPat(base)
		}
		if len(q.Parts) == 1 && !q.Global {
			if q.Parts[0].Name == "_" {
				return p.parseOrPat(&WildcardPat{Span: sp()})
			}
			return p.parseOrPat(&BindPat{Name: q.Parts[0].Name, Span: sp()})
		}
		return p.parseOrPat(&PathPat{Path: q, Span: sp()})
	}
	if p.peekLit("(") {
		p.lit("(")
		var elems []Pattern
		if !p.peekLit(")") {
			for {
				elems = append(elems, p.parsePattern())
				if _, ok := p.oneOf(","); !ok {
					break
				}
				if p.peekLit(")") {
					break
				}
			}
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
		return p.parseOrPat(&TuplePat{Elems: elems, Span: sp()})
	}
	if p.peekLit("[") {
		p.lit("[")
		var elems []Pattern
		rest := ""
		hasRest := false
		if !p.peekLit("]") && !p.peekLit("..") {
			for {
				elems = append(elems, p.parsePattern())
				if _, ok := p.oneOf(","); !ok {
					break
				}
				if p.peekLit("]") || p.peekLit("..") {
					break
				}
			}
		}
		if p.lit("..") {
			hasRest = true
			if id, ok := p.parseIdent(); ok {
				rest = id.Name
			}
		}
		if !p.lit("]") {
			p.fail("expected `]`", p.spanAt(p.pos, p.pos))
		}
		return p.parseOrPat(&ArrayPat{Elems: elems, Rest: rest, HasRest: hasRest, Span: sp()})
	}
	// literal patterns
	p.skipTrivia()
	if !p.eof() {
		c := p.src[p.pos]
		if (c >= '0' && c <= '9') || c == '"' || c == '\'' {
			return p.parseOrPat(&LitPat{Value: p.parseLit(), Span: sp()})
		}
	}
	if w, ok := p.peekIdent(); ok && (w == "true" || w == "false" || w == "null") {
		return p.parseOrPat(&LitPat{Value: p.parseLit(), Span: sp()})
	}
	p.fail("expected pattern", p.spanAt(p.pos, p.pos))
	return &WildcardPat{Span: sp()}
}

func (p *Parser) parseOrPat(first Pattern) Pattern {
	if !p.lit("|") {
		return first
	}
	alts := []Pattern{first, p.parsePattern()}
	for p.lit("|") {
		alts = append(alts, p.parsePattern())
	}
	return &OrPat{Alts: alts, Span: Merge(alts[0].Pos(), alts[len(alts)-1].Pos())}
}
