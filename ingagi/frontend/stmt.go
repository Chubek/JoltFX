package frontend

// Statement parsing (ng: stmt, block, return_stmt … on_stmt).
//
// Ingagi adds four statements over the EBNF base: `for each` query loops,
// `bind` shader-pipeline attachments, `emit` event publication, and inline
// `on` observers. All desugar cleanly: foreach → query iteration, bind →
// renderer edge, emit → event queue, on → observer registration.

var hookNames = map[string]bool{
	"startup": true, "shutdown": true, "update": true, "fixed_update": true,
	"late_update": true, "pre_render": true, "post_render": true, "on_event": true,
}

// blockDeclKeywords start item declarations that may appear in blocks.
var blockDeclKeywords = map[string]bool{
	"const": true, "type": true, "struct": true, "enum": true, "fn": true,
	"component": true, "entity": true, "system": true, "event": true,
	"resource": true, "shader": true, "pipeline": true, "interface": true,
	"extend": true, "extern": true, "async": true,
}

// peekBlockDecl reports whether a block item is an item declaration rather
// than a statement. A decl keyword directly followed by `(` is a call, not
// a declaration (`type(x)`); `@`/visibility prefixes always attempt the
// declaration parse, which rewinds safely on mismatch.
func (p *Parser) peekBlockDecl() bool {
	pos, line, col := p.mark()
	defer func() { p.pos, p.line, p.col = pos, line, col }()
	if p.peekLit("@") {
		return true
	}
	w, ok := p.peekIdent()
	if !ok {
		return false
	}
	if w == "public" || w == "private" || w == "internal" || w == "async" {
		return true
	}
	if !blockDeclKeywords[w] {
		return false
	}
	p.parseIdent()
	p.skipTrivia()
	return p.eof() || p.src[p.pos] != '('
}

// tryParseBlockDecl parses an item declaration inside a block, or returns
// nil without consuming when the item is a statement.
func (p *Parser) tryParseBlockDecl() *DeclStmt {
	pos, line, col := p.mark()
	if !p.peekBlockDecl() {
		return nil
	}
	d := p.parseExternalDecl()
	if d == nil {
		p.pos, p.line, p.col = pos, line, col
		return nil
	}
	return &DeclStmt{Decl: d, Span: p.spanFrom(pos, line, col)}
}

// parseBlock implements block. Item declarations are accepted alongside
// statements; a trailing expression without `;` is the block's value, which
// makes `if`/`match` expression arms ergonomic (`{ 1 }`). A zero-progress
// iteration fails loudly and recovers instead of spinning.
func (p *Parser) parseBlock() *Block {
	pos, line, col := p.mark()
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
		return &Block{Span: p.spanAt(pos, pos)}
	}
	var stmts []Stmt
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		ls := p.pos
		if d := p.tryParseBlockDecl(); d != nil {
			stmts = append(stmts, d)
			continue
		}
		s := p.parseStmt()
		if s == nil {
			p.fail("expected statement", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
		} else {
			stmts = append(stmts, s)
		}
		if p.pos == ls {
			p.fail("expected statement", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
			if p.pos == ls && !p.eof() {
				p.advance(1)
			}
		}
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	return &Block{Stmts: stmts, Span: p.spanFrom(pos, line, col)}
}

// parseStmt implements stmt. Leading attributes attach to `let`/`var`
// declarations (`@hot let x = ...`); anything else rewinds.
func (p *Parser) parseStmt() Stmt {
	p.skipTrivia()
	if p.peekLit("{") {
		return p.parseBlock()
	}
	if p.peekLit("@") {
		pos0, l0, c0 := p.mark()
		attrs := p.parseAttrList()
		if w2, ok := p.peekIdent(); ok && (w2 == "let" || w2 == "var") {
			p.parseIdent()
			return p.parseVarDecl(attrs, w2)
		}
		p.pos, p.line, p.col = pos0, l0, c0
	}
	w, ok := p.peekIdent()
	if !ok {
		// Expression statement starting with a non-ident (number, string…).
		return p.parseExprOrVarDecl(nil)
	}
	switch w {
	case "return":
		return p.parseReturn()
	case "break":
		return p.parseBreak()
	case "continue":
		return p.parseContinue()
	case "if":
		return p.parseIfStmt()
	case "while":
		return p.parseWhile()
	case "for":
		return p.parseFor()
	case "loop":
		pos, line, col := p.mark()
		p.parseIdent()
		return &LoopStmt{Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
	case "match":
		return p.parseMatchStmt()
	case "defer":
		pos, line, col := p.mark()
		p.parseIdent()
		return &DeferStmt{Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
	case "unsafe":
		pos, line, col := p.mark()
		p.parseIdent()
		return &UnsafeStmt{Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
	case "emit":
		pos, line, col := p.mark()
		p.parseIdent()
		e := p.parseExpr()
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
		return &EmitStmt{Event: e, Span: p.spanFrom(pos, line, col)}
	case "on":
		return p.parseOn()
	case "bind":
		return p.parseBind()
	case "let", "var":
		p.parseIdent()
		return p.parseVarDecl(nil, w)
	}
	return p.parseExprOrVarDecl(nil)
}

// parseExprOrVarDecl disambiguates `x: T = v;` (var decl, also bare `let`
// /`var` forms) from expression statements. A leading `x = ...` is always
// an assignment: the EBNF admits both readings, but assignment is the only
// non-surprising one inside function bodies (`i = i + 1` must update, not
// shadow). Undeclared names become implicit globals at runtime.
// A missing `;` before `}` is accepted (trailing block value).
func (p *Parser) parseExprOrVarDecl(attrs []Attr) Stmt {
	if name, ok := p.peekIdent(); ok && !isKeyword(name) {
		pos, line, col := p.mark()
		p.parseIdent()
		p.skipTrivia()
		// `x :` (but not `::`) opens a declaration; `=` and `;` continue
		// as expressions (assignment / value statement).
		isDecl := false
		if !p.eof() && p.src[p.pos] == ':' &&
			(p.pos+1 >= len(p.src) || p.src[p.pos+1] != ':') {
			isDecl = true
		}
		p.pos, p.line, p.col = pos, line, col
		if isDecl {
			return p.parseVarDecl(attrs, "")
		}
		_ = pos
	}
	pos, line, col := p.mark()
	e := p.parseExpr()
	if !p.lit(";") && !p.peekLit("}") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &ExprStmt{X: e, Span: p.spanFrom(pos, line, col)}
}

// parseVarDecl implements var_decl_stmt.
func (p *Parser) parseVarDecl(attrs []Attr, kind string) Stmt {
	pos, line, col := p.mark()
	name := p.expectIdent("variable name")
	var ty Type
	if p.lit(":") {
		ty = p.parseType()
	}
	var init Expr
	if p.lit("=") {
		init = p.parseExpr()
	}
	if !p.lit(";") && !p.peekLit("}") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &VarDeclStmt{Attrs: attrs, Kind: kind, Name: name.Name, Type: ty, Init: init, Span: p.spanFrom(pos, line, col)}
}

func (p *Parser) parseReturn() Stmt {
	pos, line, col := p.mark()
	p.parseIdent()
	var v Expr
	if !p.peekLit(";") {
		v = p.parseExpr()
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &ReturnStmt{Value: v, Span: p.spanFrom(pos, line, col)}
}

func (p *Parser) parseBreak() Stmt {
	pos, line, col := p.mark()
	p.parseIdent()
	label := ""
	if name, ok := p.peekIdent(); ok && !isKeyword(name) {
		// `break label value?;` — label only when followed by `;` or expr-start
		// that is not on the same… keep simple: consume as label when the next
		// token after it is `;`.
		save := p.pos
		sline, scol := p.line, p.col
		p.parseIdent()
		p.skipTrivia()
		if !p.eof() && p.src[p.pos] == ';' {
			label = name
		} else {
			p.pos, p.line, p.col = save, sline, scol
		}
	}
	var v Expr
	if !p.peekLit(";") {
		v = p.parseExpr()
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &BreakStmt{Label: label, Value: v, Span: p.spanFrom(pos, line, col)}
}

func (p *Parser) parseContinue() Stmt {
	pos, line, col := p.mark()
	p.parseIdent()
	label := ""
	if name, ok := p.peekIdent(); ok && !isKeyword(name) {
		label = name
		p.parseIdent()
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &ContinueStmt{Label: label, Span: p.spanFrom(pos, line, col)}
}

// parseIfStmt implements if_stmt.
func (p *Parser) parseIfStmt() Stmt {
	pos, line, col := p.mark()
	p.parseIdent() // if
	cond := p.parseExpr()
	then := p.parseBlock()
	var els *Block
	var elsif Stmt
	if w, ok := p.peekIdent(); ok && w == "else" {
		p.parseIdent()
		if w2, ok2 := p.peekIdent(); ok2 && w2 == "if" {
			elsif = p.parseIfStmt()
		} else {
			els = p.parseBlock()
		}
	}
	return &IfStmt{Cond: cond, Then: then, Else: els, ElseIf: elsif, Span: p.spanFrom(pos, line, col)}
}

func (p *Parser) parseWhile() Stmt {
	pos, line, col := p.mark()
	p.parseIdent() // while
	cond := p.parseExpr()
	return &WhileStmt{Cond: cond, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
}

// parseFor dispatches between foreach_stmt and for_stmt.
func (p *Parser) parseFor() Stmt {
	pos, line, col := p.mark()
	p.parseIdent() // for
	if w, ok := p.peekIdent(); ok && w == "each" {
		p.parseIdent()
		if !p.lit("(") {
			p.fail("expected `(`", p.spanAt(p.pos, p.pos))
		}
		var bindings []QueryBinding
		if !p.peekLit(")") {
			for {
				bindings = append(bindings, p.parseQueryBinding())
				if _, ok := p.oneOf(","); !ok {
					break
				}
			}
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
		if !p.lit("in") {
			p.fail("expected `in`", p.spanAt(p.pos, p.pos))
		}
		iter := p.parseExpr()
		return &ForEachStmt{Bindings: bindings, Iter: iter, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
	}
	pat := p.parsePattern()
	if !p.lit("in") {
		p.fail("expected `in`", p.spanAt(p.pos, p.pos))
	}
	iter := p.parseExpr()
	return &ForStmt{Pat: pat, Iter: iter, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
}

func (p *Parser) parseMatchStmt() Stmt {
	pos, line, col := p.mark()
	p.parseIdent() // match
	x := p.parseExpr()
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	arms := p.parseMatchArms()
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	return &MatchStmt{X: x, Arms: arms, Span: p.spanFrom(pos, line, col)}
}

// parseOn implements on_stmt.
func (p *Parser) parseOn() Stmt {
	pos, line, col := p.mark()
	p.parseIdent() // on
	kind := ""
	if w, ok := p.peekIdent(); ok && (w == "add" || w == "remove" || w == "set") {
		kind = w
		p.parseIdent()
	} else {
		p.fail("expected add|remove|set", p.spanAt(p.pos, p.pos))
	}
	comp := p.expectPath("component name")
	return &OnStmt{Kind: kind, Comp: comp, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
}

// parseBind implements bind_stmt.
func (p *Parser) parseBind() Stmt {
	pos, line, col := p.mark()
	p.parseIdent() // bind
	what := ""
	if w, ok := p.peekIdent(); ok && (w == "pipeline" || w == "shader") {
		what = w
		p.parseIdent()
	} else {
		p.fail("expected pipeline|shader", p.spanAt(p.pos, p.pos))
	}
	_ = what
	name := p.expectIdent("pipeline name").Name
	stage := ""
	if p.lit(":") {
		stage = p.expectIdent("stage name").Name
	}
	if !p.lit("to") {
		p.fail("expected `to`", p.spanAt(p.pos, p.pos))
	}
	p.lit("each")
	if !p.lit("with") {
		p.fail("expected `with`", p.spanAt(p.pos, p.pos))
	}
	if !p.lit("(") {
		p.fail("expected `(`", p.spanAt(p.pos, p.pos))
	}
	var with, without []QueryComp
	if !p.peekLit(")") {
		with = p.parseQueryComps()
	}
	if !p.lit(")") {
		p.fail("expected `)`", p.spanAt(p.pos, p.pos))
	}
	if w, ok := p.peekIdent(); ok && w == "without" {
		p.parseIdent()
		if !p.lit("(") {
			p.fail("expected `(`", p.spanAt(p.pos, p.pos))
		}
		if !p.peekLit(")") {
			without = p.parseQueryComps()
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
	}
	if !p.lit("in") {
		p.fail("expected `in`", p.spanAt(p.pos, p.pos))
	}
	iter := p.parseExpr()
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &BindStmt{Pipeline: name, Stage: stage, With: with, Without: without, Iter: iter, Span: p.spanFrom(pos, line, col)}
}
