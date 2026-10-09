// Scannerless parser driver: cursor, trivia, terminals, errors, and the
// compilation-unit loop. Expression, statement, declaration, ECS and shader
// parsing live in expr.go, stmt.go, decl.go, ecs_parse.go and shader_parse.go;
// every function documents the ingagi.ng rule it implements.
package frontend

import (
	"fmt"
	"strconv"
	"strings"
)

// Error is a parse diagnostic with a byte span.
type Error struct {
	Msg  string
	Span Span
}

func (e Error) Error() string { return fmt.Sprintf("%s: %s", e.Span, e.Msg) }

// Result is the outcome of a parse.
type Result struct {
	File   *File
	Errors []Error
}

// OK reports whether parsing succeeded without errors.
func (r *Result) OK() bool { return len(r.Errors) == 0 }

// Parse parses source as an Ingagi compilation unit.
func Parse(src string) *Result { return parseSource(src) }

// ParseExpression parses source as a single expression (REPL, attributes).
func ParseExpression(src string) (Expr, []Error) {
	p := NewParser(src)
	e := p.parseExpr()
	errs := p.errs
	if !p.eof() {
		errs = append(errs, Error{Msg: "unexpected trailing input", Span: p.spanAt(p.pos, p.pos)})
	}
	return e, errs
}

// ---------------------------------------------------------------------------
// Cursor
// ---------------------------------------------------------------------------

// Parser is a scannerless recursive-descent parser over the raw byte buffer.
// There is no lexer: terminals are matched directly at the cursor with trivia
// skipped between every token, mirroring NovoParse's scannerless model where
// overlapping terminals resolve by grammar context (longest viable match).
type Parser struct {
	src  string
	pos  int
	line int // 1-based
	col  int // 1-based, in bytes
	errs []Error
}

func NewParser(src string) *Parser { return &Parser{src: src, line: 1, col: 1} }

func (p *Parser) eof() bool { return p.pos >= len(p.src) }

// mark records the current position for span construction.
func (p *Parser) mark() (pos, line, col int) { return p.pos, p.line, p.col }

func (p *Parser) spanFrom(pos, line, col int) Span {
	return Span{Start: pos, End: p.pos, Line: line, Col: col}
}

func (p *Parser) spanAt(start, end int) Span {
	return Span{Start: start, End: end, Line: p.line, Col: p.col}
}

func (p *Parser) fail(msg string, sp Span) {
	p.errs = append(p.errs, Error{Msg: msg, Span: sp})
}

// advance moves n bytes, tracking lines/columns.
func (p *Parser) advance(n int) {
	for i := 0; i < n && p.pos < len(p.src); i++ {
		if p.src[p.pos] == '\n' {
			p.line++
			p.col = 1
		} else {
			p.col++
		}
		p.pos++
	}
}

// skipTrivia implements %skip: whitespace, // line comments, /* */ block
// comments (nestable). Called before every terminal match.
func (p *Parser) skipTrivia() {
	for !p.eof() {
		c := p.src[p.pos]
		switch {
		case c == ' ' || c == '\t' || c == '\r' || c == '\n':
			p.advance(1)
		case c == '/' && p.pos+1 < len(p.src) && p.src[p.pos+1] == '/':
			for !p.eof() && p.src[p.pos] != '\n' {
				p.advance(1)
			}
		case c == '/' && p.pos+1 < len(p.src) && p.src[p.pos+1] == '*':
			p.advance(2)
			depth := 1
			for !p.eof() && depth > 0 {
				if p.src[p.pos] == '/' && p.pos+1 < len(p.src) && p.src[p.pos+1] == '*' {
					depth++
					p.advance(2)
				} else if p.src[p.pos] == '*' && p.pos+1 < len(p.src) && p.src[p.pos+1] == '/' {
					depth--
					p.advance(2)
				} else {
					p.advance(1)
				}
			}
		default:
			return
		}
	}
}

// ---------------------------------------------------------------------------
// Terminal matching (longest-viable, scannerless)
// ---------------------------------------------------------------------------

func isIdentStart(c byte) bool {
	return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
}
func isIdentChar(c byte) bool { return isIdentStart(c) || (c >= '0' && c <= '9') }

// lit matches an exact terminal. Alphabetic terminals require a word boundary
// so `in` does not match the prefix of `internal`; symbolic terminals match
// raw, so callers try longer operators first (e.g. "<<=" before "<<").
// The match is transactional: a failed match consumes nothing (not even
// trivia), so adjacency tests and lookahead stay reliable.
func (p *Parser) lit(s string) bool {
	pos, line, col := p.mark()
	p.skipTrivia()
	if !strings.HasPrefix(p.src[p.pos:], s) {
		p.pos, p.line, p.col = pos, line, col
		return false
	}
	if last := s[len(s)-1]; isIdentChar(last) || last == '"' || last == '\'' {
		if end := p.pos + len(s); end < len(p.src) && isIdentChar(p.src[end]) {
			p.pos, p.line, p.col = pos, line, col
			return false
		}
	}
	p.advance(len(s))
	return true
}

func (p *Parser) peekLit(s string) bool {
	pos, line, col := p.mark()
	ok := p.lit(s)
	p.pos, p.line, p.col = pos, line, col
	return ok
}

// peekOp matches a symbolic operator only when it cannot extend into a
// longer operator (scannerless longest-match at the binary level): `|` must
// not steal `||`, `&` must not steal `&&`, `<` must not steal `<<`/`<=`,
// and `=` must not steal `==`.
func (p *Parser) peekOp(op string) bool {
	pos, line, col := p.mark()
	if !p.lit(op) {
		return false
	}
	ok := true
	if !p.eof() {
		n := p.src[p.pos]
		switch op {
		case "|":
			ok = n != '|'
		case "&":
			ok = n != '&'
		case "<":
			ok = n != '<' && n != '=' && n != '>'
		case ">":
			ok = n != '>' && n != '='
		case "=", "!", "+", "-", "*", "/", "%", "^":
			ok = n != '='
		case "<<", ">>", "<=", ">=", "==", "!=",
			"+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", "<<=", ">>=":
			ok = n != '='
		}
	}
	p.pos, p.line, p.col = pos, line, col
	return ok
}

// oneOf tries literals longest-first and reports which matched.
func (p *Parser) oneOf(lits ...string) (string, bool) {
	for _, s := range lits {
		if p.lit(s) {
			return s, true
		}
	}
	return "", false
}

// peekIdent returns the identifier at the cursor without consuming it.
func (p *Parser) peekIdent() (string, bool) {
	pos, line, col := p.mark()
	id, ok := p.parseIdent()
	p.pos, p.line, p.col = pos, line, col
	if !ok {
		return "", false
	}
	return id.Name, true
}

// parseIdent implements IDENT (ng: IDENT). Failed matches consume nothing;
// success spans cover exactly the identifier.
func (p *Parser) parseIdent() (Ident, bool) {
	rpos, rline, rcol := p.mark()
	p.skipTrivia()
	pos, line, col := p.mark()
	if p.eof() || !isIdentStart(p.src[p.pos]) {
		p.pos, p.line, p.col = rpos, rline, rcol
		return Ident{}, false
	}
	start := p.pos
	for !p.eof() && isIdentChar(p.src[p.pos]) {
		p.advance(1)
	}
	return Ident{Name: p.src[start:p.pos], Span: Span{Start: pos, End: p.pos, Line: line, Col: col}}, true
}

func (p *Parser) expectIdent(what string) Ident {
	if id, ok := p.parseIdent(); ok {
		return id
	}
	pos, line, col := p.mark()
	p.fail("expected "+what, Span{Start: pos, End: pos, Line: line, Col: col})
	return Ident{Span: Span{Start: pos, End: pos, Line: line, Col: col}}
}

// ---------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------

var keywords = map[string]bool{
	"module": true, "import": true, "as": true, "const": true, "type": true,
	"struct": true, "enum": true, "fn": true, "async": true, "let": true,
	"var": true, "mut": true, "return": true, "break": true, "continue": true,
	"if": true, "else": true, "while": true, "for": true, "each": true,
	"in": true, "loop": true, "match": true, "defer": true, "unsafe": true,
	"self": true, "await": true, "true": true, "false": true, "null": true,
	"public": true, "private": true, "internal": true, "extern": true,
	"extend": true, "component": true, "entity": true, "child": true,
	"system": true, "query": true, "with": true, "without": true,
	"optional": true, "before": true, "after": true, "parallel": true,
	"startup": true, "shutdown": true, "update": true, "fixed_update": true,
	"late_update": true, "pre_render": true, "post_render": true,
	"on_event": true, "event": true, "resource": true, "shader": true,
	"pipeline": true, "interface": true, "uniform": true, "storage": true,
	"buffer": true, "sampler": true, "comparison": true, "texture1d": true,
	"texture2d": true, "texture2d_array": true, "texture3d": true,
	"texture_cube": true, "texture_cube_array": true, "texture2d_ms": true,
	"storage_image": true, "read": true, "write": true, "read_write": true,
	"vertex": true, "fragment": true, "compute": true, "geometry": true,
	"tess_control": true, "tess_evaluation": true, "mesh": true,
	"task": true, "specialize": true, "layout": true, "vertex_layout": true,
	"raster": true, "depth": true, "blend": true, "target": true,
	"bind": true, "to": true, "emit": true, "on": true, "add": true,
	"remove": true, "set": true,
}

func isKeyword(s string) bool { return keywords[s] }

// ---------------------------------------------------------------------------
// Shared rules
// ---------------------------------------------------------------------------

// parseAttrList implements attr_list.
func (p *Parser) parseAttrList() []Attr {
	var out []Attr
	for {
		pos, line, col := p.mark()
		if !p.lit("@") {
			return out
		}
		name := p.expectIdent("attribute name")
		var args []Expr
		if p.lit("(") {
			if !p.peekLit(")") {
				for {
					args = append(args, p.parseExpr())
					if _, ok := p.oneOf(","); !ok {
						break
					}
				}
			}
			if !p.lit(")") {
				p.fail("expected `)`", p.spanAt(p.pos, p.pos))
			}
		}
		out = append(out, Attr{Name: name.Name, Args: args, Span: p.spanFrom(pos, line, col)})
		_ = col
	}
}

// parseVisibility implements visibility.
func (p *Parser) parseVisibility() Visibility {
	if p.peekLit("public") || p.peekLit("private") || p.peekLit("internal") {
		if s, ok := p.oneOf("public", "private", "internal"); ok {
			return Visibility(s)
		}
	}
	return VisDefault
}

// parseGenericParams implements generic_params.
func (p *Parser) parseGenericParams() []string {
	if !p.lit("<") {
		return nil
	}
	var out []string
	for {
		id := p.expectIdent("generic parameter")
		out = append(out, id.Name)
		if _, ok := p.oneOf(","); !ok {
			break
		}
	}
	if !p.lit(">") {
		p.fail("expected `>`", p.spanAt(p.pos, p.pos))
	}
	return out
}

// parsePath implements qualified_name.
func (p *Parser) parsePath() (Path, bool) {
	pos, line, col := p.mark()
	var q Path
	if p.lit("::") {
		q.Global = true
	}
	id, ok := p.parseIdent()
	if !ok {
		return Path{}, false
	}
	q.Parts = append(q.Parts, id)
	for p.lit("::") {
		id = p.expectIdent("path segment")
		q.Parts = append(q.Parts, id)
	}
	q.Span = p.spanFrom(pos, line, col)
	return q, true
}

func (p *Parser) expectPath(what string) Path {
	if q, ok := p.parsePath(); ok {
		return q
	}
	pos, line, col := p.mark()
	p.fail("expected "+what, Span{Start: pos, End: pos, Line: line, Col: col})
	return Path{}
}

// parseType implements type_expr.
func (p *Parser) parseType() Type {
	pos, line, col := p.mark()
	// & / * prefix
	if p.peekLit("&") || p.peekLit("*") {
		isRef := p.lit("&")
		if !isRef {
			p.lit("*")
		}
		mut := p.lit("mut")
		elem := p.parseType()
		sp := p.spanFrom(pos, line, col)
		if isRef {
			return &RefType{Mut: mut, Elem: elem, Span: sp}
		}
		return &PtrType{Mut: mut, Elem: elem, Span: sp}
	}
	// fn type
	if w, _ := p.peekIdent(); w == "fn" && p.peekLit("fn") {
		p.lit("fn")
		p.lit("(")
		var params []Type
		if !p.peekLit(")") {
			for {
				params = append(params, p.parseType())
				if _, ok := p.oneOf(","); !ok {
					break
				}
			}
		}
		p.lit(")")
		var ret Type
		if p.lit("->") {
			ret = p.parseType()
		}
		return &FnType{Params: params, Ret: ret, Span: p.spanFrom(pos, line, col)}
	}
	// tuple or parenthesized-qualified disambiguation: `(` always tuple here
	if p.peekLit("(") {
		p.lit("(")
		var elems []Type
		if !p.peekLit(")") {
			for {
				elems = append(elems, p.parseType())
				if _, ok := p.oneOf(","); !ok {
					break
				}
			}
		}
		if !p.lit(")") {
			p.fail("expected `)`", p.spanAt(p.pos, p.pos))
		}
		return &TupleType{Elems: elems, Span: p.spanFrom(pos, line, col)}
	}
	// array type `[T; N]`
	if p.peekLit("[") {
		p.lit("[")
		elem := p.parseType()
		if !p.lit(";") {
			p.fail("expected `;` in array type", p.spanAt(p.pos, p.pos))
			return elem
		}
		length := p.parseExpr()
		if !p.lit("]") {
			p.fail("expected `]`", p.spanAt(p.pos, p.pos))
		}
		return &ArrayType{Elem: elem, Length: length, Span: p.spanFrom(pos, line, col)}
	}
	// qualified type with generics and `?`
	q := p.expectPath("type")
	var args []Type
	if p.lit("<") {
		for {
			args = append(args, p.parseType())
			if _, ok := p.oneOf(","); !ok {
				break
			}
		}
		if !p.lit(">") {
			p.fail("expected `>`", p.spanAt(p.pos, p.pos))
		}
	}
	opt := p.lit("?")
	return &TypeName{Path: q, Args: args, Optional: opt, Span: p.spanFrom(pos, line, col)}
}

// parseTypeList implements type_list.
func (p *Parser) parseTypeList() []Type {
	var out []Type
	for {
		out = append(out, p.parseType())
		if _, ok := p.oneOf(","); !ok {
			return out
		}
	}
}

// skipQuoted advances past a '...' or "..." literal (escapes honored).
// The cursor must be at the opening quote.
func (p *Parser) skipQuoted() {
	q := p.src[p.pos]
	p.advance(1)
	for !p.eof() {
		c := p.src[p.pos]
		p.advance(1)
		if c == '\\' && !p.eof() {
			p.advance(1)
			continue
		}
		if c == q {
			return
		}
		if q == '"' && c == '\n' {
			return
		}
	}
}

// isStructTypeName reports whether a name may introduce a struct literal.
// Convention: type names start uppercase. This keeps `match Dir::South {`
// (enum scrutinee + block) out of struct-literal territory.
func isStructTypeName(name string) bool {
	return name != "" && name[0] >= 'A' && name[0] <= 'Z'
}

// tryStructBrace reports whether `{` at the cursor (after a path expression)
// opens a struct literal rather than a block. An immediately adjacent `{`
// always qualifies (`Damage{...}`); otherwise the type name must start
// uppercase and a depth-0 `:` or `,` must precede any `;`, `=>`, `}` or EOF,
// so `match x {`, `in world {` and `if Foo {}` stay control flow. The cursor
// is unchanged on return; the caller consumes the brace on success.
func (p *Parser) tryStructBrace(last string) bool {
	if !p.eof() && p.src[p.pos] == '{' {
		return true
	}
	if !p.peekLit("{") {
		return false
	}
	if !isStructTypeName(last) {
		return false
	}
	pos, line, col := p.mark()
	p.lit("{")
	depth := 0
	for !p.eof() {
		c := p.src[p.pos]
		switch {
		case c == '"' || c == '\'':
			p.skipQuoted()
		case c == '/' && p.pos+1 < len(p.src) && (p.src[p.pos+1] == '/' || p.src[p.pos+1] == '*'):
			p.skipTrivia()
		case c == '{':
			depth++
			p.advance(1)
		case c == '}':
			if depth == 0 {
				p.pos, p.line, p.col = pos, line, col
				return false
			}
			depth--
			p.advance(1)
		case c == ':' && p.pos+1 < len(p.src) && p.src[p.pos+1] == ':':
			// Path separator: never a field delimiter.
			p.advance(2)
		case c == ',':
			if depth == 0 {
				p.pos, p.line, p.col = pos, line, col
				return true
			}
			p.advance(1)
		case c == ':' && (p.pos+1 >= len(p.src) || p.src[p.pos+1] != ':'):
			if depth == 0 {
				p.pos, p.line, p.col = pos, line, col
				return true
			}
			p.advance(1)
		case c == ';':
			if depth == 0 {
				p.pos, p.line, p.col = pos, line, col
				return false
			}
			p.advance(1)
		case c == '=' && p.pos+1 < len(p.src) && p.src[p.pos+1] == '>':
			if depth == 0 {
				p.pos, p.line, p.col = pos, line, col
				return false
			}
			p.advance(2)
		default:
			p.advance(1)
		}
	}
	p.pos, p.line, p.col = pos, line, col
	return false
}

// ---------------------------------------------------------------------------
// Literals (ng: literal)
// ---------------------------------------------------------------------------

func (p *Parser) parseLit() *Lit {
	pos, line, col := p.mark()
	mk := func(k LitKind) *Lit {
		return &Lit{Kind: k, Span: p.spanFrom(pos, line, col)}
	}
	p.skipTrivia()
	// booleans / null
	if w, ok := p.peekIdent(); ok {
		switch w {
		case "true", "false":
			p.parseIdent()
			l := mk(LitBool)
			l.Bool = w == "true"
			return l
		case "null":
			p.parseIdent()
			return mk(LitNull)
		}
	}
	if p.eof() {
		p.fail("expected literal", p.spanAt(p.pos, p.pos))
		return mk(LitNull)
	}
	c := p.src[p.pos]
	// strings
	if c == '"' {
		s := p.parseStringLit()
		l := mk(LitString)
		l.Str = s
		return l
	}
	// chars
	if c == '\'' {
		r := p.parseCharLit()
		l := mk(LitChar)
		l.Int = int64(r)
		return l
	}
	// numbers
	if (c >= '0' && c <= '9') || (c == '.' && p.pos+1 < len(p.src) && p.src[p.pos+1] >= '0' && p.src[p.pos+1] <= '9') {
		return p.parseNumberLit(pos, line, col)
	}
	p.fail("expected literal", p.spanAt(p.pos, p.pos))
	return mk(LitNull)
}

func (p *Parser) parseStringLit() string {
	p.skipTrivia()
	var b strings.Builder
	p.advance(1) // opening quote
	for !p.eof() {
		c := p.src[p.pos]
		if c == '"' {
			p.advance(1)
			return b.String()
		}
		if c == '\n' {
			p.fail("unterminated string", p.spanAt(p.pos, p.pos))
			return b.String()
		}
		if c == '\\' {
			p.advance(1)
			if p.eof() {
				break
			}
			e := p.src[p.pos]
			p.advance(1)
			switch e {
			case 'n':
				b.WriteByte('\n')
			case 'r':
				b.WriteByte('\r')
			case 't':
				b.WriteByte('\t')
			case '0':
				b.WriteByte(0)
			case '\\', '"', '\'':
				b.WriteByte(e)
			case 'x':
				hex := ""
				for i := 0; i < 2 && !p.eof(); i++ {
					hex += string(p.src[p.pos])
					p.advance(1)
				}
				v, err := strconv.ParseUint(hex, 16, 8)
				if err != nil {
					p.fail("bad \\x escape", p.spanAt(p.pos, p.pos))
				} else {
					b.WriteByte(byte(v))
				}
			case 'u':
				if !p.lit("{") {
					p.fail("expected `\\u{...}`", p.spanAt(p.pos, p.pos))
					break
				}
				hex := ""
				for !p.eof() && p.src[p.pos] != '}' {
					hex += string(p.src[p.pos])
					p.advance(1)
				}
				p.lit("}")
				v, err := strconv.ParseUint(hex, 16, 32)
				if err != nil {
					p.fail("bad \\u escape", p.spanAt(p.pos, p.pos))
				} else {
					b.WriteRune(rune(v))
				}
			default:
				p.fail(fmt.Sprintf("unknown escape `\\%c`", e), p.spanAt(p.pos, p.pos))
			}
			continue
		}
		b.WriteByte(c)
		p.advance(1)
	}
	p.fail("unterminated string", p.spanAt(p.pos, p.pos))
	return b.String()
}

func (p *Parser) parseCharLit() rune {
	p.skipTrivia()
	p.advance(1) // opening quote
	if p.eof() {
		p.fail("unterminated character", p.spanAt(p.pos, p.pos))
		return 0
	}
	var r rune
	if p.src[p.pos] == '\\' {
		p.advance(1)
		if p.eof() {
			return 0
		}
		switch e := p.src[p.pos]; e {
		case 'n':
			r = '\n'
		case 'r':
			r = '\r'
		case 't':
			r = '\t'
		case '0':
			r = 0
		case '\\', '\'', '"':
			r = rune(e)
		default:
			r = rune(e)
		}
		p.advance(1)
	} else {
		r = rune(p.src[p.pos])
		p.advance(1)
	}
	if !p.eof() && p.src[p.pos] == '\'' {
		p.advance(1)
	} else {
		p.fail("expected `'`", p.spanAt(p.pos, p.pos))
	}
	return r
}

func (p *Parser) parseNumberLit(pos, line, col int) *Lit {
	p.skipTrivia()
	mk := func(k LitKind) *Lit {
		return &Lit{Kind: k, Span: p.spanFrom(pos, line, col)}
	}
	rest := p.src[p.pos:]
	clean := func(s string) string { return strings.ReplaceAll(s, "_", "") }
	// prefixed integers
	if strings.HasPrefix(rest, "0x") || strings.HasPrefix(rest, "0X") ||
		strings.HasPrefix(rest, "0b") || strings.HasPrefix(rest, "0B") ||
		strings.HasPrefix(rest, "0o") || strings.HasPrefix(rest, "0O") {
		start := p.pos
		p.advance(2)
		for !p.eof() && (isIdentChar(p.src[p.pos])) {
			p.advance(1)
		}
		v, err := strconv.ParseInt(clean(p.src[start:p.pos]), 0, 64)
		if err != nil {
			p.fail("bad integer literal", p.spanFrom(pos, line, col))
			return mk(LitInt)
		}
		l := mk(LitInt)
		l.Int = v
		return l
	}
	start := p.pos
	for !p.eof() && ((p.src[p.pos] >= '0' && p.src[p.pos] <= '9') || p.src[p.pos] == '_') {
		p.advance(1)
	}
	isFloat := false
	if !p.eof() && p.src[p.pos] == '.' && !(p.pos+1 < len(p.src) && p.src[p.pos+1] == '.') {
		// `1.` only floats when a digit follows (else it is member access `a.0`? no —
		// keep grammar-faithful: require digit after dot for floats here; a lone
		// dot is left for member access).
		if p.pos+1 < len(p.src) && p.src[p.pos+1] >= '0' && p.src[p.pos+1] <= '9' {
			isFloat = true
			p.advance(1)
			for !p.eof() && ((p.src[p.pos] >= '0' && p.src[p.pos] <= '9') || p.src[p.pos] == '_') {
				p.advance(1)
			}
		}
	}
	if !p.eof() && (p.src[p.pos] == 'e' || p.src[p.pos] == 'E') {
		isFloat = true
		p.advance(1)
		if !p.eof() && (p.src[p.pos] == '+' || p.src[p.pos] == '-') {
			p.advance(1)
		}
		if p.eof() || !(p.src[p.pos] >= '0' && p.src[p.pos] <= '9') {
			p.fail("bad float exponent", p.spanFrom(pos, line, col))
			return mk(LitFloat)
		}
		for !p.eof() && ((p.src[p.pos] >= '0' && p.src[p.pos] <= '9') || p.src[p.pos] == '_') {
			p.advance(1)
		}
	}
	text := clean(p.src[start:p.pos])
	if isFloat {
		v, err := strconv.ParseFloat(text, 64)
		if err != nil {
			p.fail("bad float literal", p.spanFrom(pos, line, col))
			return mk(LitFloat)
		}
		l := mk(LitFloat)
		l.Float = v
		return l
	}
	v, err := strconv.ParseInt(text, 10, 64)
	if err != nil {
		p.fail("bad integer literal", p.spanFrom(pos, line, col))
		return mk(LitInt)
	}
	l := mk(LitInt)
	l.Int = v
	return l
}

// ---------------------------------------------------------------------------
// compilation_unit (ng: compilation_unit)
// ---------------------------------------------------------------------------

// ParseFile parses a full compilation unit with error recovery: after a bad
// declaration the cursor advances to the next `;`, `}` or declaration keyword.
func (p *Parser) ParseFile() *Result {
	pos, line, col := p.mark()
	f := &File{Span: Span{Start: 0, Line: 1, Col: 1}}
	if w, ok := p.peekIdent(); ok && w == "module" {
		p.parseIdent()
		f.Module = p.expectPath("module name")
		if !p.lit(";") {
			p.fail("expected `;`", p.spanAt(p.pos, p.pos))
		}
	}
	for !p.eof() {
		p.skipTrivia()
		if p.eof() {
			break
		}
		if w, ok := p.peekIdent(); ok && w == "import" {
			p.parseIdent()
			imp := ImportDecl{Span: p.spanFrom(pos, line, col)}
			imp.Path = p.expectPath("import path")
			if w2, ok2 := p.peekIdent(); ok2 && w2 == "as" {
				p.parseIdent()
				imp.Alias = p.expectIdent("import alias").Name
			}
			if !p.lit(";") {
				p.fail("expected `;`", p.spanAt(p.pos, p.pos))
			}
			f.Imports = append(f.Imports, imp)
			continue
		}
		declStart := p.pos
		if d := p.parseExternalDecl(); d != nil {
			f.Decls = append(f.Decls, d)
		} else {
			// Recovery records one diagnostic (no silent acceptance), then
			// skips to the next boundary with guaranteed progress.
			p.skipTrivia()
			if !p.eof() {
				p.fail("expected declaration", p.spanAt(p.pos, p.pos))
			}
			p.skipToBoundary()
			if p.pos == declStart && !p.eof() {
				p.advance(1)
				p.skipToBoundary()
			}
		}
	}
	f.Span.End = p.pos
	return &Result{File: f, Errors: p.errs}
}

var declKeywords = map[string]bool{
	"const": true, "type": true, "struct": true, "enum": true, "fn": true,
	"async": true, "component": true, "entity": true, "system": true,
	"event": true, "resource": true, "shader": true, "pipeline": true,
	"interface": true, "extend": true, "extern": true, "public": true,
	"private": true, "internal": true, "@": true,
}

func (p *Parser) skipToBoundary() {
	depth := 0
	for !p.eof() {
		p.skipTrivia()
		if p.eof() {
			return
		}
		c := p.src[p.pos]
		if c == '{' {
			depth++
			p.advance(1)
			continue
		}
		if c == '}' {
			if depth == 0 {
				return
			}
			depth--
			p.advance(1)
			continue
		}
		if c == ';' && depth == 0 {
			p.advance(1)
			return
		}
		if depth == 0 {
			if w, ok := p.peekIdent(); ok && declKeywords[w] {
				return
			}
			if p.peekLit("@") {
				return
			}
		}
		p.advance(1)
	}
}
