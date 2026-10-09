package frontend

// Shader and pipeline parsing (ng: shader_decl … state_assign).
//
// Shaders are first-class declarations: resource bindings, helper functions,
// specialization constants and per-stage bodies share the host statement and
// expression grammar, so one parser serves CPU and GPU code alike. The shader
// compiler (shader/) lowers these nodes to GLSL/WGSL/HLSL.

var shaderStages = map[string]bool{
	"vertex": true, "fragment": true, "compute": true, "geometry": true,
	"tess_control": true, "tess_evaluation": true, "mesh": true, "task": true,
}

var textureKinds = map[string]bool{
	"texture1d": true, "texture2d": true, "texture2d_array": true,
	"texture3d": true, "texture_cube": true, "texture_cube_array": true,
	"texture2d_ms": true,
}

var shaderAccess = map[string]bool{"read": true, "write": true, "read_write": true}

// parseShaderDecl implements shader_decl.
func (p *Parser) parseShaderDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // shader
	name := p.expectIdent("shader name")
	var opts []ShaderOption
	if p.peekLit("(") {
		p.lit("(")
		if !p.peekLit(")") {
			for {
				opos, oline, ocol := p.mark()
				oname := p.expectIdent("option name")
				if !p.lit(":") {
					p.fail("expected `:`", p.spanAt(p.pos, p.pos))
				}
				opts = append(opts, ShaderOption{Name: oname.Name, Value: p.parseExpr(), Span: p.spanFrom(opos, oline, ocol)})
				if _, ok := p.oneOf(","); !ok {
					break
				}
			}
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
	}
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &ShaderDecl{Attrs: attrs, Vis: vis, Name: name.Name, Options: opts}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		ls := p.pos
		mpos, mline, mcol := p.mark()
		w, _ := p.peekIdent()
		switch {
		case w == "struct" || w == "interface":
			d.Structs = append(d.Structs, p.parseShaderStruct(mpos, mline, mcol))
		case w == "const":
			d.Consts = append(d.Consts, p.parseShaderConst())
		case w == "specialize":
			p.parseIdent()
			n := p.expectIdent("constant name")
			if !p.lit(":") {
				p.fail("expected `:`", p.spanAt(p.pos, p.pos))
			}
			ty := p.parseShaderType()
			var v Expr
			if p.lit("=") {
				v = p.parseExpr()
			}
			if !p.lit(";") {
				p.fail("expected `;`", p.spanAt(p.pos, p.pos))
			}
			d.Specs = append(d.Specs, ShaderSpecConst{Name: n.Name, Type: ty, Value: v, Span: p.spanFrom(mpos, mline, mcol)})
		case shaderStages[w]:
			d.Stages = append(d.Stages, p.parseShaderStage(mpos, mline, mcol))
		case w == "fn":
			d.Funcs = append(d.Funcs, p.parseShaderFunc(true))
		default:
			// Resource (optional attributes) or bare `name(params)` function.
			attrs := p.parseAttrList()
			w2, _ := p.peekIdent()
			if w2 == "fn" {
				d.Funcs = append(d.Funcs, p.parseShaderFunc(true))
				continue
			}
			if r, ok := p.tryParseShaderResource(attrs, mpos, mline, mcol); ok {
				d.Resources = append(d.Resources, r)
				continue
			}
			d.Funcs = append(d.Funcs, p.parseShaderFunc(false))
		}
		if p.pos == ls {
			p.fail("expected shader member", p.spanAt(p.pos, p.pos))
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

// parseShaderStruct implements shader_struct (and shader interface bodies,
// which share the layout purpose).
func (p *Parser) parseShaderStruct(pos, line, col int) ShaderStruct {
	p.parseIdent() // struct | interface
	name := p.expectIdent("struct name")
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	s := ShaderStruct{Name: name.Name}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		s.Fields = append(s.Fields, p.parseShaderField())
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	s.Span = p.spanFrom(pos, line, col)
	return s
}

// parseShaderField implements shader_field.
func (p *Parser) parseShaderField() ShaderField {
	pos, line, col := p.mark()
	attrs := p.parseAttrList()
	name := p.expectIdent("field name")
	if !p.lit(":") {
		p.fail("expected `:`", p.spanAt(p.pos, p.pos))
	}
	ty := p.parseShaderType()
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return ShaderField{Attrs: attrs, Name: name.Name, Type: ty, Span: p.spanFrom(pos, line, col)}
}

// parseShaderConst implements shader_const.
func (p *Parser) parseShaderConst() ShaderConst {
	pos, line, col := p.mark()
	p.parseIdent() // const
	name := p.expectIdent("constant name")
	if !p.lit(":") {
		p.fail("expected `:`", p.spanAt(p.pos, p.pos))
	}
	ty := p.parseShaderType()
	if !p.lit("=") {
		p.fail("expected `=`", p.spanAt(p.pos, p.pos))
	}
	v := p.parseExpr()
	if !p.lit(";") {
		p.fail("expected `;`", p.spanAt(p.pos, p.pos))
	}
	return ShaderConst{Name: name.Name, Type: ty, Value: v, Span: p.spanFrom(pos, line, col)}
}

// tryParseShaderResource attempts shader_resource_body. It returns ok=false
// without consuming when the member is actually a function.
func (p *Parser) tryParseShaderResource(attrs []Attr, pos, line, col int) (ShaderResource, bool) {
	save := p.pos
	sline, scol := p.line, p.col
	fail := func() (ShaderResource, bool) {
		p.pos, p.line, p.col = save, sline, scol
		return ShaderResource{}, false
	}
	w, ok := p.peekIdent()
	if !ok {
		p.pos, p.line, p.col = save, sline, scol
		return ShaderResource{}, false
	}
	r := ShaderResource{Attrs: attrs}
	switch {
	case w == "uniform":
		p.parseIdent()
		r.Kind = "uniform"
		r.Name = p.expectIdent("block name").Name
		if !p.lit("{") {
			p.fail("expected `{`", p.spanAt(p.pos, p.pos))
			return r, true
		}
		for !p.peekLit("}") && !p.eof() {
			r.Fields = append(r.Fields, p.parseShaderField())
		}
		p.lit("}")
		if name, ok := p.peekIdent(); ok && !isKeyword(name) {
			p.parseIdent()
			r.Alias = name
		}
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
		r.Span = p.spanFrom(pos, line, col)
		return r, true
	case w == "storage" || w == "read" || w == "write" || w == "read_write":
		// storage buffer, optionally prefixed with access
		if shaderAccess[w] {
			r.Access = w
			p.parseIdent()
			if w2, _ := p.peekIdent(); w2 != "storage" {
				// Actually `storage_image` handled below; `read sampler`? no.
				return fail()
			}
		}
		if w2, _ := p.peekIdent(); w2 == "storage" {
			// Disambiguate `storage buffer` vs anything else.
			p.parseIdent()
			if w3, _ := p.peekIdent(); w3 == "buffer" {
				p.parseIdent()
				r.Kind = "storage"
				r.Name = p.expectIdent("block name").Name
				if !p.lit("{") {
					p.fail("expected `{`", p.spanAt(p.pos, p.pos))
					return r, true
				}
				for !p.peekLit("}") && !p.eof() {
					r.Fields = append(r.Fields, p.parseShaderField())
				}
				p.lit("}")
				if name, ok := p.peekIdent(); ok && !isKeyword(name) {
					p.parseIdent()
					r.Alias = name
				}
				if !p.lit(";") {
					p.fail("expected `;`", p.spanAt(p.pos, p.pos))
				}
				r.Span = p.spanFrom(pos, line, col)
				return r, true
			}
			return fail()
		}
		return fail()
	case w == "storage_image":
		p.parseIdent()
		kind, _ := p.peekIdent()
		if !textureKinds[kind] {
			p.fail("expected texture kind", p.spanAt(p.pos, p.pos))
			return r, true
		}
		p.parseIdent()
		r.Kind = "storage_image"
		r.Type = kind
		r.Name = p.expectIdent("image name").Name
		if !p.lit(":") {
			p.fail("expected `:`", p.spanAt(p.pos, p.pos))
		}
		if a, _ := p.peekIdent(); shaderAccess[a] {
			r.Access = a
			p.parseIdent()
		} else {
			p.fail("expected access qualifier", p.spanAt(p.pos, p.pos))
		}
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
		r.Span = p.spanFrom(pos, line, col)
		return r, true
	case textureKinds[w]:
		p.parseIdent()
		r.Kind = w
		r.Name = p.expectIdent("texture name").Name
		if p.lit(":") {
			r.Type = p.parseShaderType()
		}
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
		r.Span = p.spanFrom(pos, line, col)
		return r, true
	case w == "sampler" || w == "comparison":
		if w == "comparison" {
			p.parseIdent()
			r.Kind = "comparison_sampler"
			if w2, _ := p.peekIdent(); w2 != "sampler" {
				p.fail("expected `sampler`", p.spanAt(p.pos, p.pos))
				return r, true
			}
			p.parseIdent()
		} else {
			p.parseIdent()
			r.Kind = "sampler"
		}
		r.Name = p.expectIdent("sampler name").Name
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
		r.Span = p.spanFrom(pos, line, col)
		return r, true
	}
	return fail()
}

// parseShaderType implements shader_type (kept as text; sema resolves it).
func (p *Parser) parseShaderType() string {
	if p.peekLit("[") {
		p.lit("[")
		elem := p.parseShaderType()
		p.lit(";")
		// Length is a host expression; keep its source text.
		lpos := p.pos
		p.parseExpr()
		ltext := p.src[lpos:p.pos]
		p.lit("]")
		return "[" + elem + ";" + ltext + "]"
	}
	id := p.expectIdent("shader type")
	name := id.Name
	if p.lit("<") {
		name += "<"
		for {
			tpos := p.pos
			p.parseType()
			name += p.src[tpos:p.pos]
			if _, ok := p.oneOf(","); !ok {
				break
			}
			name += ","
		}
		p.lit(">")
		name += ">"
	}
	return name
}

// parseShaderFunc implements shader_func; withFn records `fn` presence.
func (p *Parser) parseShaderFunc(withFn bool) ShaderFunc {
	pos, line, col := p.mark()
	if withFn {
		p.parseIdent() // fn
	}
	name := p.expectIdent("function name")
	if !p.lit("(") {
		p.fail("expected `(`", p.spanAt(p.pos, p.pos))
	}
	f := ShaderFunc{Name: name.Name}
	if !p.peekLit(")") {
		for {
			fpos, fline, fcol := p.mark()
			attrs := p.parseAttrList()
			pname := p.expectIdent("parameter name")
			if !p.lit(":") {
				p.fail("expected `:`", p.spanAt(p.pos, p.pos))
			}
			f.Params = append(f.Params, ShaderParam{Attrs: attrs, Name: pname.Name, Type: p.parseShaderType(), Span: p.spanFrom(fpos, fline, fcol)})
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
	if p.lit("->") {
		f.Ret = p.parseShaderType()
	}
	f.Body = p.parseBlock()
	f.Span = p.spanFrom(pos, line, col)
	return f
}

// parseShaderStage implements shader_stage.
func (p *Parser) parseShaderStage(pos, line, col int) ShaderStage {
	kind := p.expectIdent("stage name").Name
	opts := map[string]Expr{}
	if p.peekLit("(") {
		p.lit("(")
		if !p.peekLit(")") {
			for {
				oname := p.expectIdent("option name")
				if !p.lit(":") {
					p.fail("expected `:`", p.spanAt(p.pos, p.pos))
				}
				opts[oname.Name] = p.parseExpr()
				if _, ok := p.oneOf(","); !ok {
					break
				}
			}
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
	}
	return ShaderStage{Kind: kind, Options: opts, Body: p.parseBlock(), Span: p.spanFrom(pos, line, col)}
}

// parsePipelineDecl implements pipeline_decl.
func (p *Parser) parsePipelineDecl(attrs []Attr, vis Visibility, pos, line, col int) Decl {
	p.parseIdent() // pipeline
	name := p.expectIdent("pipeline name")
	if !p.lit("{") {
		p.fail("expected `{`", p.spanAt(p.pos, p.pos))
	}
	d := &PipelineDecl{Attrs: attrs, Vis: vis, Name: name.Name, Stages: map[string]Path{}}
	for {
		p.skipTrivia()
		if p.eof() || p.peekLit("}") {
			break
		}
		w, _ := p.peekIdent()
		switch {
		case shaderStages[w]:
			p.parseIdent()
			if !p.lit(":") {
				p.fail("expected `:`", p.spanAt(p.pos, p.pos))
			}
			d.Stages[w] = p.expectPath("shader stage")
			if !p.lit(";") {
				p.fail("expected `;`", p.spanAt(p.pos, p.pos))
			}
		case w == "layout" || w == "vertex_layout":
			p.parseIdent()
			if !p.lit(":") {
				p.fail("expected `:`", p.spanAt(p.pos, p.pos))
			}
			q := p.expectPath("layout")
			if w == "layout" {
				d.Layout = q
			} else {
				d.VertexLayout = q
			}
			if !p.lit(";") {
				p.fail("expected `;`", p.spanAt(p.pos, p.pos))
			}
		case w == "raster" || w == "depth" || w == "blend" || w == "target" || w == "compute":
			p.parseIdent()
			if !p.lit("{") {
				p.fail("expected `{`", p.spanAt(p.pos, p.pos))
			}
			m := map[string]Expr{}
			for !p.peekLit("}") && !p.eof() {
				sls := p.pos
				sname := p.expectIdent("state key")
				if !p.lit(":") {
					p.fail("expected `:`", p.spanAt(p.pos, p.pos))
				}
				m[sname.Name] = p.parseExpr()
				if !p.lit(";") {
					p.fail("expected `;`", p.spanAt(p.pos, p.pos))
				}
				if p.pos == sls {
					p.fail("expected state assignment", p.spanAt(p.pos, p.pos))
					p.skipToBoundary()
					if p.pos == sls && !p.eof() {
						p.advance(1)
					}
				}
			}
			if !p.lit("}") {
				p.fail("expected `}`", p.spanAt(p.pos, p.pos))
			}
			switch w {
			case "raster":
				d.Raster = m
			case "depth":
				d.Depth = m
			case "blend":
				d.Blend = m
			case "target":
				d.Target = m
			case "compute":
				d.Compute = m
			}
		default:
			p.fail("expected pipeline member", p.spanAt(p.pos, p.pos))
			p.skipToBoundary()
		}
	}
	if !p.lit("}") {
		p.fail("expected `}`", p.spanAt(p.pos, p.pos))
	}
	d.Span = p.spanFrom(pos, line, col)
	return d
}
