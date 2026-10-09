package runtime

import "math"

// Constant pool and code tape. The pool interns every literal, name and
// descriptor the tape references by index, which keeps instructions small
// and makes AOT serialization a straight memory dump (see aot.go).

// ConstPool holds interned constants.
type ConstPool struct {
	Ints    []int64
	Floats  []float64
	Strings []string
	intIdx  map[int64]int32
	fltIdx  map[uint64]int32
	strIdx  map[string]int32
}

// NewConstPool returns an empty pool.
func NewConstPool() *ConstPool {
	return &ConstPool{
		intIdx: map[int64]int32{},
		fltIdx: map[uint64]int32{},
		strIdx: map[string]int32{},
	}
}

// InternInt interns an integer.
func (p *ConstPool) InternInt(v int64) int32 {
	if i, ok := p.intIdx[v]; ok {
		return i
	}
	i := int32(len(p.Ints))
	p.Ints = append(p.Ints, v)
	p.intIdx[v] = i
	return i
}

// InternFloat interns a float (bit-exact key).
func (p *ConstPool) InternFloat(v float64) int32 {
	k := math.Float64bits(v)
	if i, ok := p.fltIdx[k]; ok {
		return i
	}
	i := int32(len(p.Floats))
	p.Floats = append(p.Floats, v)
	p.fltIdx[k] = i
	return i
}

// InternString interns a string.
func (p *ConstPool) InternString(v string) int32 {
	if i, ok := p.strIdx[v]; ok {
		return i
	}
	i := int32(len(p.Strings))
	p.Strings = append(p.Strings, v)
	p.strIdx[v] = i
	return i
}

// Int resolves an int constant.
func (p *ConstPool) Int(i int) int64 { return p.Ints[i] }

// Float resolves a float constant.
func (p *ConstPool) Float(i int) float64 { return p.Floats[i] }

// String resolves a string constant.
func (p *ConstPool) String(i int) string { return p.Strings[i] }

// Tape is a compiled instruction stream with its entry points.
type Tape struct {
	Code  []Instr
	Pool  *ConstPool
	Entry map[string]int // function name -> code offset
}

// NewTape returns an empty tape.
func NewTape() *Tape {
	return &Tape{Pool: NewConstPool(), Entry: map[string]int{}}
}

// Builder assembles a tape with label patching.
type Builder struct {
	code  []Instr
	pool  *ConstPool
	entry map[string]int
}

// NewBuilder returns a tape builder over pool.
func NewBuilder(pool *ConstPool) *Builder {
	return &Builder{pool: pool, entry: map[string]int{}}
}

// Pos returns the next instruction offset.
func (b *Builder) Pos() int { return len(b.code) }

// Emit appends an instruction and returns its offset.
func (b *Builder) Emit(op Op, a, c int32) int {
	b.code = append(b.code, Instr{Op: op, A: a, C: c})
	return len(b.code) - 1
}

// EmitB appends an instruction with A and B operands.
func (b *Builder) EmitB(op Op, a, bb, c int32) int {
	b.code = append(b.code, Instr{Op: op, A: a, B: bb, C: c})
	return len(b.code) - 1
}

// Patch sets instruction at's A operand (jump target).
func (b *Builder) Patch(at int, target int) { b.code[at].A = int32(target) }

// MarkEntry records a function entry offset.
func (b *Builder) MarkEntry(name string) { b.entry[name] = len(b.code) }

// Build finalizes the tape.
func (b *Builder) Build() *Tape {
	return &Tape{Code: b.code, Pool: b.pool, Entry: b.entry}
}

// Disassemble renders the whole tape.
func (t *Tape) Disassemble() []string {
	out := make([]string, len(t.Code))
	for i, in := range t.Code {
		out[i] = itoa(int64(i)) + ": " + in.Disassemble(t.Pool)
	}
	return out
}
