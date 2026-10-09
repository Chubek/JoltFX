package frontend

// ECS declaration parsing (ng: component_decl … resource_decl, plus the
// query_binding / query_comp shared rules used by systems, foreach and bind).

// parseComponentDecl implements component_decl.
func (p *Parser) parseComponentDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // component
	name := p.expectIdent("component name")
	params := p.parseGenericParams()
	var base Type
	if p.lit(":") {
		base = p.parseType()
	}
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &ComponentDecl{Attrs: attrs, Vis: vis, Name: name.Name, Params: params, Base: base}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		ls := p.pos
		mattrs := p.parseAttrList()
		if w, _ := p.peekIdent(); w == "fn" || w == "async" {
			isAsync := false
			if w == "async" {
				p.parseIdent()
				isAsync = true
			}
			fpos, fline, fcol := p.mark()
			d.Methods = append(d.Methods, p.parseFnDecl(mattrs, "", isAsync, fpos, fline, fcol))
			continue
		}
		f := p.parseComponentField()
		f.Attrs = append(mattrs, f.Attrs...)
		d.Fields = append(d.Fields, f)
		if p.pos == ls {
			p.fail("expected component member", p.spanAt(p.pos, p.pos))
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

// parseEntityDecl implements entity_decl.
func (p *Parser) parseEntityDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // entity
	name := p.expectIdent("entity name")
	params := p.parseGenericParams()
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &EntityDecl{Attrs: attrs, Vis: vis, Name: name.Name, Params: params}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		mpos, mline, mcol := p.mark()
		if w, ok := p.peekIdent(); ok && w == "child" {
			p.parseIdent()
			cname := p.expectIdent("child name")
			if !p.lit(":") {
				p.fail("expected `:`", p.spanAt(p.pos, p.pos))
			}
			val := p.parseEntityExpr()
			if !p.lit(";") {
				p.fail("expected `;`", p.spanAt(p.pos, p.pos))
			}
			d.Children = append(d.Children, EntityChild{Name: cname.Name, Value: val, Span: p.spanFrom(mpos, mline, mcol)})
			continue
		}
		// Property (`name: Type [= v];`) vs attachment (`Type [{...}];`):
		// an ident followed by a single `:` is a property.
		if q, ok := p.peekPath(); ok {
			p.parsePath()
			p.skipTrivia()
			if len(q.Parts) == 1 && !q.Global && !p.eof() && p.src[p.pos] == ':' &&
				(p.pos+1 >= len(p.src) || p.src[p.pos+1] != ':') {
				p.lit(":")
				ty := p.parseType()
				var v Expr
				if p.lit("=") {
					v = p.parseExpr()
				}
				if !p.lit(";") {
					p.fail("expected `;`", p.spanAt(p.pos, p.pos))
				}
				d.Props = append(d.Props, EntityProp{Name: q.Parts[0].Name, Type: ty, Value: v, Span: p.spanFrom(mpos, mline, mcol)})
				continue
			}
			var inits []StructInit
			if p.peekLit("{") {
				p.lit("{")
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
			}
			if !p.lit(";") {
				p.fail("expected `;`", p.spanAt(p.pos, p.pos))
			}
			d.Attachments = append(d.Attachments, EntityAttachment{Type: q, Inits: inits, Span: p.spanFrom(mpos, mline, mcol)})
			continue
		}
		p.fail("expected entity member", p.spanAt(p.pos, p.pos))
		p.skipToBoundary()
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseEntityExpr implements entity_expr.
func (p *Parser) parseEntityExpr() Expr {
	pos, line, col := p.mark()
	q := p.expectPath("entity expression")
	if p.peekLit("(") {
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
		return &CallExpr{Fn: &PathExpr{Path: q, Span: q.Span}, Args: args, Span: p.spanFrom(pos, line, col)}
	}
	if p.peekLit("{") {
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

// parseSystemDecl implements system_decl.
func (p *Parser) parseSystemDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // system
	name := p.expectIdent("system name")
	params := p.parseGenericParams()
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &SystemDecl{Attrs: attrs, Vis: vis, Name: name.Name, Params: params}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		mpos, mline, mcol := p.mark()
		w, _ := p.peekIdent()
		switch w {
		case "query":
			d.Queries = append(d.Queries, p.parseSystemQuery(mpos, mline, mcol))
		case "before", "after", "parallel":
			d.Deps = append(d.Deps, p.parseSystemDep(mpos, mline, mcol))
		case "fn", "async":
			isAsync := false
			if w == "async" {
				p.parseIdent()
				isAsync = true
			}
			fpos, fline, fcol := p.mark()
			d.Fns = append(d.Fns, p.parseFnDecl(nil, "", isAsync, fpos, fline, fcol))
		default:
			if hookNames[w] {
				d.Hooks = append(d.Hooks, p.parseSystemHook(mpos, mline, mcol))
				continue
			}
			// Could be a plain `fn` with visibility prefix, or a variable.
			if w == "public" || w == "private" || w == "internal" || w == "let" || w == "var" {
				if s := p.parseStmt(); s != nil {
					if vd, ok := s.(*VarDeclStmt); ok {
						d.Vars = append(d.Vars, vd)
					}
				}
				continue
			}
			p.fail("expected system member", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
		}
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}

// parseSystemQuery implements system_query + query_filter.
func (p *Parser) parseSystemQuery(pos, line, col int) SystemQuery {
	p.parseIdent() // query
	binding := p.expectIdent("query binding").Name
	q := SystemQuery{Binding: binding, Span: p.spanFrom(pos, line, col)}
	for {
		w, ok := p.peekIdent()
		if !ok || (w != "with" && w != "without" && w != "optional") {
			break
		}
		p.parseIdent()
		if !p.lit("(") {
			p.fail("expected `(`", p.spanAt(p.pos, p.pos))
		}
		var comps []QueryComp
		if !p.peekLit(")") {
			comps = p.parseQueryComps()
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
		switch w {
		case "with":
			q.With = comps
		case "without":
			q.Without = comps
		case "optional":
			q.Optional = comps
		}
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	q.Span = p.spanFrom(pos, line, col)
	return q
}

// parseQueryComps implements query_comps.
func (p *Parser) parseQueryComps() []QueryComp {
	var out []QueryComp
	for {
		out = append(out, p.parseQueryComp())
		if _, ok := p.oneOf(","); !ok {
			return out
		}
	}
}

// parseQueryComp implements query_comp.
func (p *Parser) parseQueryComp() QueryComp {
	pos, line, col := p.mark()
	byRef, mut := false, false
	if p.lit("&") {
		byRef = true
		if w, ok := p.peekIdent(); ok && w == "mut" {
			mut = true
			p.parseIdent()
		}
	}
	ty := p.expectPath("component type")
	alias := ""
	if w, ok := p.peekIdent(); ok && w == "as" {
		p.parseIdent()
		alias = p.expectIdent("alias").Name
	}
	return QueryComp{Mut: mut, ByRef: byRef, Type: ty, Alias: alias, Span: p.spanFrom(pos, line, col)}
}

// parseQueryBinding implements query_binding.
func (p *Parser) parseQueryBinding() QueryBinding {
	pos, line, col := p.mark()
	byRef, mut := false, false
	if p.lit("&") {
		byRef = true
		if w, ok := p.peekIdent(); ok && w == "mut" {
			mut = true
			p.parseIdent()
		}
	}
	name := p.expectIdent("component name").Name
	alias := ""
	if w, ok := p.peekIdent(); ok && w == "as" {
		p.parseIdent()
		alias = p.expectIdent("alias").Name
	}
	return QueryBinding{Mut: mut, ByRef: byRef, Name: name, Alias: alias, Span: p.spanFrom(pos, line, col)}
}

// parseSystemHook implements system_hook.
func (p *Parser) parseSystemHook(pos, line, col int) SystemHook {
	kind := p.expectIdent("hook name").Name
	if !p.lit("(") {
		p.fail("expected `(`", p.spanAt(p.pos, p.pos))
	}
	var params []Param
	if !p.peekLit(")") {
		for {
			params = append(params, p.parseParam())
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
	return SystemHook{Kind: kind, Params: params, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
}

// parseSystemDep implements system_dep.
func (p *Parser) parseSystemDep(pos, line, col int) SystemDep {
	kind := p.expectIdent("dependency").Name
	target := ""
	if kind == "parallel" {
		if w, ok := p.peekIdent(); ok && (w == "true" || w == "false") {
			target = w
			p.parseIdent()
		} else {
			p.fail("expected true|false", p.spanAt(p.pos, p.pos))
		}
	} else {
		target = p.expectPath("system name").String()
	}
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return SystemDep{Kind: kind, Target: target, Span: p.spanFrom(pos, line, col)}
}
