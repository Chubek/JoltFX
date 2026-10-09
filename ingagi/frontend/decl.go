package frontend

// Declaration parsing (ng: external_decl … interface_decl).
// parseExternalDecl is called by the compilation-unit loop after attributes.

// parseExternalDecl implements external_decl. It returns nil (without
// consuming) when no declaration starts at the cursor.
func (p *Parser) parseExternalDecl() Decl {
	start, sline, scol := p.mark()
	attrs := p.parseAttrList()
	vis := p.parseVisibility()
	rewind := func() Decl {
		p.pos, p.line, p.col = start, sline, scol
		return nil
	}
	w, ok := p.peekIdent()
	if !ok {
		return rewind()
	}
	// `async fn` — async belongs to the function.
	isAsync := false
	if w == "async" {
		save := p.pos
		sline, scol := p.line, p.col
		p.parseIdent()
		if w2, ok2 := p.peekIdent(); ok2 && w2 == "fn" {
			isAsync = true
		} else {
			p.pos, p.line, p.col = save, sline, scol
		}
		w, _ = p.peekIdent()
	}
	pos, line, col := p.mark()
	switch w {
	case "const":
		return p.parseConstDecl(attrs, vis, pos, line, col)
	case "type":
		return p.parseTypeAlias(attrs, vis, pos, line, col)
	case "struct":
		return p.parseStructDecl(attrs, vis, pos, line, col)
	case "enum":
		return p.parseEnumDecl(attrs, vis, pos, line, col)
	case "fn":
		return p.parseFnDecl(attrs, vis, isAsync, pos, line, col)
	case "component":
		return p.parseComponentDecl(attrs, vis, pos, line, col)
	case "entity":
		return p.parseEntityDecl(attrs, vis, pos, line, col)
	case "system":
		return p.parseSystemDecl(attrs, vis, pos, line, col)
	case "event":
		return p.parseEventDecl(attrs, vis, pos, line, col)
	case "resource":
		return p.parseResourceDecl(attrs, vis, pos, line, col)
	case "shader":
		return p.parseShaderDecl(attrs, vis, pos, line, col)
	case "pipeline":
		return p.parsePipelineDecl(attrs, vis, pos, line, col)
	case "interface":
		return p.parseInterfaceDecl(attrs, vis, pos, line, col)
	case "extend":
		return p.parseExtendDecl(pos, line, col)
	case "extern":
		return p.parseExternDecl(pos, line, col)
	}
	// Not a declaration after all: rewind so the caller can try a
	// statement parse or run its own recovery. Half-written declarations
	// record errors in their specific parse functions, never here.
	return rewind()
}

// parseConstDecl implements const_decl.
func (p *Parser) parseConstDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // const
	name := p.expectIdent("constant name")
	var ty Type
	if p.lit(":") {
		ty = p.parseType()
	}
	if !p.lit("=") {
		p.fail("expected `=`", p.spanAt(p.pos, p.pos))
	}
	v := p.parseExpr()
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &ConstDecl{Attrs: attrs, Vis: vis, Name: name.Name, Type: ty, Value: v, Span: p.spanFrom(pos, line, col)}
}

// parseTypeAlias implements type_alias_decl.
func (p *Parser) parseTypeAlias(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // type
	name := p.expectIdent("type name")
	params := p.parseGenericParams()
	if !p.lit("=") {
		p.fail("expected `=`", p.spanAt(p.pos, p.pos))
	}
	target := p.parseType()
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &TypeAliasDecl{Attrs: attrs, Vis: vis, Name: name.Name, Params: params, Target: target, Span: p.spanFrom(pos, line, col)}
}

// parseStructDecl implements struct_decl.
func (p *Parser) parseStructDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // struct
	name := p.expectIdent("struct name")
	params := p.parseGenericParams()
	var bases []Type
	if p.lit(":") {
		bases = p.parseTypeList()
	}
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &StructDecl{Attrs: attrs, Vis: vis, Name: name.Name, Params: params, Bases: bases}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		ls := p.pos
		mattrs := p.parseAttrList()
		mvis := p.parseVisibility()
		if w, _ := p.peekIdent(); w == "fn" || w == "async" {
			isAsync := false
			if w == "async" {
				p.parseIdent()
				isAsync = true
			}
			fpos, fline, fcol := p.mark()
			d.Methods = append(d.Methods, p.parseFnDecl(mattrs, mvis, isAsync, fpos, fline, fcol))
			continue
		}
		fpos, fline, fcol := p.mark()
		fname := p.expectIdent("field name")
		if !p.lit(":") {
			p.fail("expected `:`", p.spanAt(p.pos, p.pos))
		}
		ty := p.parseType()
		var init Expr
		if p.lit("=") {
			init = p.parseExpr()
		}
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
		_ = init
		d.Fields = append(d.Fields, StructField{Attrs: mattrs, Name: fname.Name, Type: ty, Span: p.spanFrom(fpos, fline, fcol)})
		if p.pos == ls {
			p.fail("expected struct member", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
			if p.pos == ls && !p.eof() {
				p.advance(1)
			}
		}
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseEnumDecl implements enum_decl.
func (p *Parser) parseEnumDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // enum
	name := p.expectIdent("enum name")
	var repr Type
	if p.lit(":") {
		repr = p.parseType()
	}
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &EnumDecl{Attrs: attrs, Vis: vis, Name: name.Name, Repr: repr}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		vpos, vline, vcol := p.mark()
		vname := p.expectIdent("variant name")
		var types []Type
		if p.lit("(") {
			types = p.parseTypeList()
			if !p.lit(")") {
				p.fail("expected `)`", p.spanAt(p.pos, p.pos))
			}
		}
		var val Expr
		if p.lit("=") {
			val = p.parseExpr()
		}
		d.Variants = append(d.Variants, EnumVariant{Name: vname.Name, Types: types, Value: val, Span: p.spanFrom(vpos, vline, vcol)})
		if _, ok := p.oneOf(","); !ok {
			break
		}
	}
	p.lit(",")
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseParam implements param.
func (p *Parser) parseParam() Param {
	pos, line, col := p.mark()
	attrs := p.parseAttrList()
	mut := false
	if w, ok := p.peekIdent(); ok && w == "mut" {
		mut = true
		p.parseIdent()
	}
	name := p.expectIdent("parameter name")
	if !p.lit(":") {
		p.fail("expected `:`", p.spanAt(p.pos, p.pos))
	}
	ty := p.parseType()
	var def Expr
	if p.lit("=") {
		def = p.parseExpr()
	}
	return Param{Attrs: attrs, Mut: mut, Name: name.Name, Type: ty, Default: def, Span: p.spanFrom(pos, line, col)}
}

// parseFnDecl implements func_decl. The cursor must be at `fn` (async
// already consumed by the caller).
func (p *Parser) parseFnDecl(attrs []Attr, vis Visibility, async bool, pos, line, col int) *FnDecl {
	p.parseIdent() // fn
	name := p.expectIdent("function name")
	params := p.parseGenericParams()
	if !p.lit("(") {
		p.fail("expected `(`", p.spanAt(p.pos, p.pos))
	}
	var args []Param
	if !p.peekLit(")") {
		for {
			args = append(args, p.parseParam())
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
	var ret Type
	if p.lit("->") {
		ret = p.parseType()
	}
	var body *Block
	if p.peekLit("{") {
		body = p.parseBlock()
	} else if !p.lit(";") {
		p.fail("expected block or `;`", p.spanAt(p.pos, p.pos))
	}
	return &FnDecl{Attrs: attrs, Vis: vis, Async: async, Name: name.Name, Params: params, Args: args, Ret: ret, Body: body, Span: p.spanFrom(pos, line, col)}
}

// parseExternDecl implements extern_decl.
func (p *Parser) parseExternDecl(pos, line, col int) Decl {
	p.parseIdent() // extern
	p.skipTrivia()
	lib := p.parseStringLit()
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &ExternDecl{Lib: lib}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		ls := p.pos
		if w, _ := p.peekIdent(); w == "fn" || w == "async" {
			isAsync := false
			if w == "async" {
				p.parseIdent()
				isAsync = true
			}
			fpos, fline, fcol := p.mark()
			d.Funcs = append(d.Funcs, p.parseFnDecl(nil, "", isAsync, fpos, fline, fcol))
			continue
		}
		if s := p.parseStmt(); s != nil {
			if vd, ok := s.(*VarDeclStmt); ok {
				d.Vars = append(d.Vars, vd)
			}
		}
		if p.pos == ls {
			p.fail("expected extern member", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
			if p.pos == ls && !p.eof() {
				p.advance(1)
			}
		}
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseExtendDecl implements extend_decl.
func (p *Parser) parseExtendDecl(pos, line, col int) Decl {
	p.parseIdent() // extend
	target := p.parseType()
	return &ExtendDecl{Target: target, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
}

// parseInterfaceDecl implements interface_decl.
func (p *Parser) parseInterfaceDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // interface
	name := p.expectIdent("interface name")
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &InterfaceDecl{Attrs: attrs, Vis: vis, Name: name.Name}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		ls := p.pos
		if w, _ := p.peekIdent(); w == "fn" || w == "async" {
			isAsync := false
			if w == "async" {
				p.parseIdent()
				isAsync = true
			}
			fpos, fline, fcol := p.mark()
			d.Methods = append(d.Methods, p.parseFnDecl(nil, "", isAsync, fpos, fline, fcol))
			continue
		}
		if s := p.parseStmt(); s != nil {
			if vd, ok := s.(*VarDeclStmt); ok {
				d.Vars = append(d.Vars, vd)
			}
		}
		if p.pos == ls {
			p.fail("expected interface member", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
			if p.pos == ls && !p.eof() {
				p.advance(1)
			}
		}
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseEventDecl implements event_decl.
func (p *Parser) parseEventDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // event
	name := p.expectIdent("event name")
	params := p.parseGenericParams()
	var base Type
	if p.lit(":") {
		base = p.parseType()
	}
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &EventDecl{Attrs: attrs, Vis: vis, Name: name.Name, Params: params, Base: base}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		d.Fields = append(d.Fields, p.parseComponentField())
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseResourceDecl implements resource_decl.
func (p *Parser) parseResourceDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // resource
	name := p.expectIdent("resource name")
	var ty Type
	if p.lit(":") {
		ty = p.parseType()
	}
	var v Expr
	if p.lit("=") {
		v = p.parseExpr()
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return &ResourceDecl{Attrs: attrs, Vis: vis, Name: name.Name, Type: ty, Value: v, Span: p.spanFrom(pos, line, col)}
}

// parseComponentField implements component_field.
func (p *Parser) parseComponentField() ComponentField {
	pos, line, col := p.mark()
	attrs := p.parseAttrList()
	name := p.expectIdent("field name")
	if !p.lit(":") {
		p.fail("expected `:`", p.spanAt(p.pos, p.pos))
	}
	ty := p.parseType()
	var def Expr
	if p.lit("=") {
		def = p.parseExpr()
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return ComponentField{Attrs: attrs, Name: name.Name, Type: ty, Default: def, Span: p.spanFrom(pos, line, col)}
}
