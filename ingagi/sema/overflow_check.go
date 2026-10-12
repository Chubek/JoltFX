package sema

import (
	"fmt"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"math/big"
)

// CheckOverflow evaluates constant integer subexpressions at arbitrary
// precision. Runtime integers wrap; overflow is a warning, while guaranteed
// division by zero or invalid shift counts are errors.
func CheckOverflow(f *fe.File, _ *Registry) []Diagnostic {
	v := &overflowVisitor{}
	fe.Walk(v, f)
	return v.out
}

type overflowVisitor struct {
	fe.BaseVisitor
	out []Diagnostic
}

func (v *overflowVisitor) VisitExpr(e fe.Expr) bool {
	n, err := constantInt(e)
	if err != nil {
		v.out = append(v.out, Diagnostic{Sev: Error, Code: "E0702", Msg: err.Error(), Span: e.Pos()})
		return false
	}
	if n != nil && !n.IsInt64() {
		v.out = append(v.out, Diagnostic{Sev: Warning, Code: "W0701", Msg: "constant integer expression overflows int64", Span: e.Pos()})
		return false
	}
	return true
}
func constantInt(e fe.Expr) (*big.Int, error) {
	switch e := e.(type) {
	case *fe.Lit:
		if e.Kind == fe.LitInt || e.Kind == fe.LitChar {
			return big.NewInt(e.Int), nil
		}
	case *fe.UnaryExpr:
		x, err := constantInt(e.X)
		if x == nil || err != nil {
			return x, err
		}
		switch e.Op {
		case "-":
			return x.Neg(x), nil
		case "+":
			return x, nil
		case "~":
			return x.Not(x), nil
		}
	case *fe.BinaryExpr:
		x, err := constantInt(e.X)
		if err != nil {
			return nil, err
		}
		y, err := constantInt(e.Y)
		if err != nil {
			return nil, err
		}
		if (e.Op == "/" || e.Op == "%") && y != nil && y.Sign() == 0 {
			return nil, fmt.Errorf("constant division by zero")
		}
		if (e.Op == "<<" || e.Op == ">>") && y != nil && (!y.IsInt64() || y.Int64() < 0 || y.Int64() > 63) {
			return nil, fmt.Errorf("constant shift count outside 0..63")
		}
		if x == nil || y == nil {
			return nil, nil
		}
		switch e.Op {
		case "+":
			return x.Add(x, y), nil
		case "-":
			return x.Sub(x, y), nil
		case "*":
			return x.Mul(x, y), nil
		case "/":
			return x.Quo(x, y), nil
		case "%":
			return x.Rem(x, y), nil
		case "&":
			return x.And(x, y), nil
		case "|":
			return x.Or(x, y), nil
		case "^":
			return x.Xor(x, y), nil
		case "<<":
			return x.Lsh(x, uint(y.Int64())), nil
		case ">>":
			return x.Rsh(x, uint(y.Int64())), nil
		}
	}
	return nil, nil
}
