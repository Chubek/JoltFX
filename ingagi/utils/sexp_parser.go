// Package utils holds small shared helpers for the Ingagi toolchain:
// a portable S-expression reader (used for NovoParse IR, target specs and
// rewrite rules), docstring extraction, and REPL line-editing helpers.
//
// Ingagi's S-expression dialect is the interchange format between the
// NovoParse frontend (grammar IR), the compiler target specs, the term
// rewriter, and the binary bundle manifest. One tiny reader serves all of
// them so the toolchain stays dependency-free and embeddable.
package utils

import (
	"fmt"
	"strconv"
	"strings"
)

// Sexp is a single S-expression node: either an atom (Atom != "") or a list.
// Numbers stay atoms; callers convert with Int/Float helpers.
type Sexp struct {
	Atom string
	List []*Sexp
	// Line records the 1-based source line where the node started.
	Line int
}

// IsAtom reports whether s is an atom.
func (s *Sexp) IsAtom() bool { return s != nil && s.List == nil }

// IsList reports whether s is a list.
func (s *Sexp) IsList() bool { return s != nil && s.List != nil }

// String renders s in canonical minimal form.
func (s *Sexp) String() string {
	if s == nil {
		return "()"
	}
	if s.IsAtom() {
		return s.Atom
	}
	var b strings.Builder
	b.WriteByte('(')
	for i, e := range s.List {
		if i > 0 {
			b.WriteByte(' ')
		}
		b.WriteString(e.String())
	}
	b.WriteByte(')')
	return b.String()
}

// Int converts an atom to int64.
func (s *Sexp) Int() (int64, error) {
	if !s.IsAtom() {
		return 0, fmt.Errorf("sexp: not an atom: %s", s.String())
	}
	return strconv.ParseInt(s.Atom, 0, 64)
}

// Float converts an atom to float64.
func (s *Sexp) Float() (float64, error) {
	if !s.IsAtom() {
		return 0, fmt.Errorf("sexp: not an atom: %s", s.String())
	}
	return strconv.ParseFloat(s.Atom, 64)
}

// Field returns the elements following a leading atom key in an
// association-style list, e.g. (rewrite NAME PATTERN REPLACEMENT).
// It returns nil when the list does not start with key.
func (s *Sexp) Field(key string) []*Sexp {
	if !s.IsList() || len(s.List) == 0 || !s.List[0].IsAtom() || s.List[0].Atom != key {
		return nil
	}
	return s.List[1:]
}

// Parse parses one or more top-level S-expressions.
func Parse(src string) ([]*Sexp, error) {
	p := &parser{s: src}
	var out []*Sexp
	for {
		p.skipTrivia()
		if p.eof() {
			return out, nil
		}
		n, err := p.parseOne()
		if err != nil {
			return nil, err
		}
		out = append(out, n)
	}
}

// ParseOne parses exactly one S-expression; trailing input is an error.
func ParseOne(src string) (*Sexp, error) {
	nodes, err := Parse(src)
	if err != nil {
		return nil, err
	}
	if len(nodes) != 1 {
		return nil, fmt.Errorf("sexp: expected 1 expression, got %d", len(nodes))
	}
	return nodes[0], nil
}

type parser struct {
	s string
	i int
	l int // current line, 1-based
}

func (p *parser) eof() bool { return p.i >= len(p.s) }

func (p *parser) skipTrivia() {
	for !p.eof() {
		c := p.s[p.i]
		if c == ' ' || c == '\t' || c == '\r' || c == '\n' {
			if c == '\n' {
				p.l++
			}
			p.i++
			continue
		}
		if c == ';' {
			for !p.eof() && p.s[p.i] != '\n' {
				p.i++
			}
			continue
		}
		return
	}
}

func (p *parser) parseOne() (*Sexp, error) {
	if p.eof() {
		return nil, fmt.Errorf("sexp: unexpected end of input")
	}
	c := p.s[p.i]
	switch {
	case c == '(':
		return p.parseList()
	case c == '"':
		return p.parseString()
	case c == '\'':
		// Quote sugar: 'x => (quote x)
		p.i++
		p.skipTrivia()
		inner, err := p.parseOne()
		if err != nil {
			return nil, err
		}
		return &Sexp{List: []*Sexp{{Atom: "quote"}, inner}, Line: p.l}, nil
	default:
		return p.parseAtom(), nil
	}
}

func (p *parser) parseList() (*Sexp, error) {
	line := p.l + 1 // l is 0-based internally until first use; normalize below
	_ = line
	startLine := p.l + 1
	p.i++ // consume '('
	var list []*Sexp
	for {
		p.skipTrivia()
		if p.eof() {
			return nil, fmt.Errorf("sexp: unterminated list")
		}
		if p.s[p.i] == ')' {
			p.i++
			return &Sexp{List: list, Line: startLine}, nil
		}
		if p.s[p.i] == '.' && (p.i+1 >= len(p.s) || isSpace(p.s[p.i+1]) || p.s[p.i+1] == ')') {
			return nil, fmt.Errorf("sexp: dotted pairs are not supported")
		}
		n, err := p.parseOne()
		if err != nil {
			return nil, err
		}
		list = append(list, n)
	}
}

func isSpace(c byte) bool { return c == ' ' || c == '\t' || c == '\r' || c == '\n' }

func (p *parser) parseString() (*Sexp, error) {
	var b strings.Builder
	p.i++ // consume opening quote
	for {
		if p.eof() {
			return nil, fmt.Errorf("sexp: unterminated string")
		}
		c := p.s[p.i]
		p.i++
		switch c {
		case '"':
			return &Sexp{Atom: strconv.Quote(b.String()), Line: p.l + 1}, nil
		case '\\':
			if p.eof() {
				return nil, fmt.Errorf("sexp: unterminated escape")
			}
			e := p.s[p.i]
			p.i++
			switch e {
			case 'n':
				b.WriteByte('\n')
			case 't':
				b.WriteByte('\t')
			case 'r':
				b.WriteByte('\r')
			case '"', '\\':
				b.WriteByte(e)
			default:
				b.WriteByte('\\')
				b.WriteByte(e)
			}
		default:
			if c == '\n' {
				p.l++
			}
			b.WriteByte(c)
		}
	}
}

// parseAtom reads a bare atom. String literals keep their quotes encoded via
// strconv.Quote so Atom round-trips; use Unquote to decode.
func (p *parser) parseAtom() *Sexp {
	start := p.i
	for !p.eof() {
		c := p.s[p.i]
		if isSpace(c) || c == '(' || c == ')' || c == '"' || c == ';' {
			break
		}
		p.i++
	}
	return &Sexp{Atom: p.s[start:p.i], Line: p.l + 1}
}

// Unquote decodes an atom produced from a string literal.
func Unquote(atom string) (string, error) {
	if len(atom) >= 2 && atom[0] == '"' {
		return strconv.Unquote(atom)
	}
	return atom, nil
}
