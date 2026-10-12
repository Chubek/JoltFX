package runtime

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"io"
	"math"
	"sort"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// AOT: ahead-of-time serialization of compiled modules.
//
// Encode snapshots a LOADED vm's executable state — tape, pool, entries,
// arities, globals and materialized field defaults — to a versioned binary
// image. Decode restores an equivalent module without running <init> or
// needing the compiler, which is the deployment path for shipped game
// scripts. Entity prefabs need no special casing: they compiled to
// `prefab::` entries like everything else.
//
// Decode bounds collection lengths and value nesting and checks structural
// tape invariants. This is not a full bytecode verifier or a sandbox.

const (
	aotMagic   = "IGBC1\x00"
	aotVersion = 1
)

type aotWriter struct {
	b     bytes.Buffer
	err   error
	depth int
}

func (w *aotWriter) u64(v uint64) {
	if w.err == nil {
		w.err = binary.Write(&w.b, binary.LittleEndian, v)
	}
}

func (w *aotWriter) i64(v int64) {
	if w.err == nil {
		w.err = binary.Write(&w.b, binary.LittleEndian, v)
	}
}

func (w *aotWriter) str(s string) {
	w.u64(uint64(len(s)))
	if w.err == nil {
		_, w.err = w.b.WriteString(s)
	}
}

func (w *aotWriter) strs(ss []string) {
	w.u64(uint64(len(ss)))
	for _, s := range ss {
		w.str(s)
	}
}

// Encode serializes the loaded state of vm.
func (vm *VM) Encode() ([]byte, error) {
	if vm.tape == nil {
		return nil, fmt.Errorf("ingagi: nothing loaded")
	}
	w := &aotWriter{}
	w.b.WriteString(aotMagic)
	w.u64(aotVersion)
	pool := vm.tape.Pool
	w.u64(uint64(len(pool.Ints)))
	for _, v := range pool.Ints {
		w.i64(v)
	}
	w.u64(uint64(len(pool.Floats)))
	for _, v := range pool.Floats {
		w.u64(math.Float64bits(v))
	}
	w.strs(pool.Strings)
	// Code.
	w.u64(uint64(len(vm.tape.Code)))
	for _, in := range vm.tape.Code {
		w.b.WriteByte(byte(in.Op))
		w.i64(int64(in.A))
		w.i64(int64(in.B))
		w.i64(int64(in.C))
	}
	// Entries + arities.
	w.u64(uint64(len(vm.funcs)))
	for _, k := range sortedKeys(vm.funcs) {
		v := vm.funcs[k]
		w.str(k)
		w.u64(uint64(v))
		w.u64(uint64(vm.params[k]))
	}
	// Globals snapshot.
	if err := w.writeValueMap(vm.globals); err != nil {
		return nil, err
	}
	// Types: kinds, fields, variants, methods, materialized defaults.
	w.u64(uint64(len(vm.types)))
	for _, name := range sortedKeys(vm.types) {
		ti := vm.types[name]
		w.strs([]string{name, ti.Kind})
		fields := make([]string, len(ti.Fields))
		for i, f := range ti.Fields {
			fields[i] = f.Name
		}
		w.strs(fields)
		w.strs(ti.Variants)
		w.u64(uint64(len(ti.Methods)))
		for _, k := range sortedKeys(ti.Methods) {
			v := ti.Methods[k]
			w.strs([]string{k, v})
		}
		if err := w.writeValueMap(vm.defs[name]); err != nil {
			return nil, err
		}
	}
	// Hooks, systems, prefabs, shaders, pipelines, events.
	var hooks []string
	for _, h := range vm.moduleHooks() {
		hooks = append(hooks, h.System, h.Kind, h.Entry, h.Event)
	}
	w.u64(uint64(len(hooks)) / 4)
	for _, h := range hooks {
		w.str(h)
	}
	w.strs(vm.moduleSystems())
	w.strs(vm.modulePrefabs())
	w.strs(vm.moduleStrings("shaders"))
	w.strs(vm.moduleStrings("pipelines"))
	w.strs(vm.moduleStrings("events"))
	if w.err != nil {
		return nil, w.err
	}
	return w.b.Bytes(), nil
}

// moduleHooks/systems/prefabs/strings are filled by LoadModule/LoadDecoded
// into small side tables so Encode need not reach back into Modules.
func (vm *VM) moduleHooks() []HookInfo { return vm.aotHooks }
func (vm *VM) moduleSystems() []string { return vm.aotSystems }
func (vm *VM) modulePrefabs() []string { return vm.aotPrefabs }
func (vm *VM) moduleStrings(k string) []string {
	if vm.aotStrings == nil {
		return nil
	}
	return vm.aotStrings[k]
}

// Value tags for the image format.
const (
	vNil = iota
	vBool
	vInt
	vFloat
	vString
	vVec2
	vVec3
	vVec4
	vMat4
	vQuat
	vArray
	vMap
	vStruct
	vEnum
	vClosure
	vBuiltin
	vEntity
)

func (w *aotWriter) writeValue(v Value) error {
	if w.depth >= aotMaxDepth {
		return fmt.Errorf("ingagi: cannot serialize cyclic or deeply nested value")
	}
	w.depth++
	defer func() { w.depth-- }()
	w.b.WriteByte(byte(vTag(v)))
	switch v.K {
	case KNil:
	case KBool:
		if v.B {
			w.b.WriteByte(1)
		} else {
			w.b.WriteByte(0)
		}
	case KInt:
		w.i64(v.I)
	case KFloat:
		w.u64(math.Float64bits(v.F))
	case KString:
		w.str(v.S)
	case KVec2, KVec3, KVec4, KQuat:
		for _, f := range v.V {
			w.u64(math.Float64bits(f))
		}
	case KMat4:
		m := v.O.(Mat4)
		for _, f := range m {
			w.u64(math.Float64bits(f))
		}
	case KArray:
		elems := v.O.(*ArrayObj).Elems
		w.u64(uint64(len(elems)))
		for _, e := range elems {
			if err := w.writeValue(e); err != nil {
				return err
			}
		}
	case KMap:
		m := v.O.(*MapObj).Fields
		keys := make([]string, 0, len(m))
		for k := range m {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		w.u64(uint64(len(keys)))
		for _, k := range keys {
			w.str(k)
			if err := w.writeValue(m[k]); err != nil {
				return err
			}
		}
	case KStruct:
		s := v.O.(*StructObj)
		w.str(s.Type)
		keys := make([]string, 0, len(s.Fields))
		for k := range s.Fields {
			keys = append(keys, k)
		}
		sort.Strings(keys)
		w.u64(uint64(len(keys)))
		for _, k := range keys {
			w.str(k)
			if err := w.writeValue(s.Fields[k]); err != nil {
				return err
			}
		}
	case KEnum:
		e := v.O.(*EnumObj)
		w.str(e.Type)
		w.str(e.Variant)
		w.u64(uint64(len(e.Payload)))
		for _, p := range e.Payload {
			if err := w.writeValue(p); err != nil {
				return err
			}
		}
	case KClosure:
		cl := v.O.(*ClosureObj)
		if len(cl.Up) != 0 {
			return fmt.Errorf("ingagi: image v1 cannot serialize captured closure %q", cl.Name)
		}
		w.str(cl.Name)
	case KBuiltin:
		w.str(v.O.(*BuiltinObj).Name)
	case KEntity:
		e := v.O.(EntityRef)
		w.u64(uint64(e.Index))
		w.u64(uint64(e.Gen))
	default:
		return fmt.Errorf("ingagi: cannot serialize %s", v.K)
	}
	return w.err
}

func vTag(v Value) int {
	switch v.K {
	case KNil:
		return vNil
	case KBool:
		return vBool
	case KInt:
		return vInt
	case KFloat:
		return vFloat
	case KString:
		return vString
	case KVec2:
		return vVec2
	case KVec3:
		return vVec3
	case KVec4:
		return vVec4
	case KMat4:
		return vMat4
	case KQuat:
		return vQuat
	case KArray:
		return vArray
	case KMap:
		return vMap
	case KStruct:
		return vStruct
	case KEnum:
		return vEnum
	case KClosure:
		return vClosure
	case KBuiltin:
		return vBuiltin
	case KEntity:
		return vEntity
	}
	return -1
}

func (w *aotWriter) writeValueMap(m map[string]Value) error {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	w.u64(uint64(len(keys)))
	for _, k := range keys {
		w.str(k)
		if err := w.writeValue(m[k]); err != nil {
			return fmt.Errorf("global %q: %w", k, err)
		}
	}
	return w.err
}

// ---------------------------------------------------------------------------
// Decode
// ---------------------------------------------------------------------------

// Reader limits bound amplification of image input into Go allocations.
const (
	aotMaxString  = 1 << 24 // bytes in one string
	aotMaxItems   = 1 << 16 // elements in one collection
	aotMaxEntries = 1 << 16 // functions, globals and types
	aotMaxCode    = 1 << 24 // instructions
	aotMaxDepth   = 64      // nested value depth
)

type aotReader struct {
	b     *bytes.Reader
	err   error
	depth int
}

// remaining reports how many bytes are still unread.
func (r *aotReader) remaining() int64 { return int64(r.b.Len()) }

// need reports whether n more bytes are available.
func (r *aotReader) need(n int) error {
	if r.err != nil {
		return r.err
	}
	if int64(n) > r.remaining() {
		r.err = io.ErrUnexpectedEOF
		return r.err
	}
	return nil
}

func (r *aotReader) u64() uint64 {
	var v uint64
	if r.need(8) != nil {
		return 0
	}
	if err := binary.Read(r.b, binary.LittleEndian, &v); err != nil {
		r.err = err
	}
	return v
}

func (r *aotReader) i64() int64 {
	var v int64
	if r.need(8) != nil {
		return 0
	}
	if err := binary.Read(r.b, binary.LittleEndian, &v); err != nil {
		r.err = err
	}
	return v
}

func (r *aotReader) byte() byte {
	b, err := r.b.ReadByte()
	if err != nil && r.err == nil {
		r.err = err
	}
	return b
}

// count validates an element count against a format limit and against the
// bytes left in the image. Every element occupies at least one byte, so a
// count larger than the remaining input is corrupt.
func (r *aotReader) count(limit uint64, what string) (uint64, error) {
	n := r.u64()
	if r.err != nil {
		return 0, r.err
	}
	if n > limit {
		return 0, fmt.Errorf("ingagi: image %s count %d exceeds limit %d", what, n, limit)
	}
	if n > uint64(r.remaining()) {
		return 0, fmt.Errorf("ingagi: image %s count %d exceeds %d remaining bytes", what, n, r.remaining())
	}
	return n, nil
}

func (r *aotReader) str() (string, error) {
	n, err := r.count(aotMaxString, "string")
	if err != nil {
		return "", err
	}
	if n == 0 {
		return "", nil
	}
	buf := make([]byte, n)
	if _, err := io.ReadFull(r.b, buf); err != nil {
		r.err = err
		return "", err
	}
	return string(buf), nil
}

func (r *aotReader) strs() ([]string, error) {
	n, err := r.count(aotMaxItems, "string list")
	if err != nil {
		return nil, err
	}
	if n > uint64(r.remaining()/8) {
		return nil, io.ErrUnexpectedEOF
	}
	out := make([]string, 0, n)
	for i := uint64(0); i < n; i++ {
		s, err := r.str()
		if err != nil {
			return nil, err
		}
		out = append(out, s)
	}
	return out, nil
}

// Decoded is an AOT image ready to load without the compiler.
type Decoded struct {
	Module  *Module
	Globals map[string]Value
	Defs    map[string]map[string]Value
}

// Decode parses an image produced by Encode.
func Decode(data []byte) (*Decoded, error) {
	if len(data) < len(aotMagic) {
		return nil, fmt.Errorf("ingagi: image is %d bytes, too short for a header", len(data))
	}
	if string(data[:len(aotMagic)]) != aotMagic {
		return nil, fmt.Errorf("ingagi: bad image magic")
	}
	// The reader starts after the magic so remaining() always means "bytes left".
	r := &aotReader{b: bytes.NewReader(data[len(aotMagic):])}
	if v := r.u64(); v != aotVersion {
		return nil, fmt.Errorf("ingagi: image version %d, want %d", v, aotVersion)
	}
	pool := NewConstPool()
	nInts, err := r.count(aotMaxItems, "int pool")
	if err != nil {
		return nil, err
	}
	if nInts > uint64(r.remaining()/8) {
		return nil, io.ErrUnexpectedEOF
	}
	for i := uint64(0); i < nInts; i++ {
		pool.Ints = append(pool.Ints, r.i64())
	}
	nFloats, err := r.count(aotMaxItems, "float pool")
	if err != nil {
		return nil, err
	}
	if nFloats > uint64(r.remaining()/8) {
		return nil, io.ErrUnexpectedEOF
	}
	for i := uint64(0); i < nFloats; i++ {
		pool.Floats = append(pool.Floats, math.Float64frombits(r.u64()))
	}
	if pool.Strings, err = r.strs(); err != nil {
		return nil, err
	}
	// Rebuild interning maps.
	for i, v := range pool.Ints {
		pool.intIdx[v] = int32(i)
	}
	for i, v := range pool.Floats {
		pool.fltIdx[math.Float64bits(v)] = int32(i)
	}
	for i, v := range pool.Strings {
		pool.strIdx[v] = int32(i)
	}
	nCode, err := r.count(aotMaxCode, "code")
	if err != nil {
		return nil, err
	}
	// Each instruction is one opcode byte plus three 8-byte operands.
	if nCode > uint64(r.remaining()/25) {
		return nil, fmt.Errorf("ingagi: image declares %d instructions but %d bytes remain", nCode, r.remaining())
	}
	code := make([]Instr, 0, nCode)
	for i := uint64(0); i < nCode; i++ {
		op := r.byte()
		a, b, c := r.i64(), r.i64(), r.i64()
		for _, operand := range []int64{a, b, c} {
			if operand < math.MinInt32 || operand > math.MaxInt32 {
				return nil, fmt.Errorf("ingagi: instruction operand outside int32")
			}
		}
		code = append(code, Instr{Op: Op(op), A: int32(a), B: int32(b), C: int32(c)})
	}
	m := &Module{
		Tape:      &Tape{Code: code, Pool: pool, Entry: map[string]int{}},
		Types:     map[string]*TypeInfo{},
		Entities:  map[string]*fe.EntityDecl{},
		Params:    map[string]int{},
		Resources: map[string]Value{},
	}
	nEntries, err := r.count(aotMaxEntries, "entry")
	if err != nil {
		return nil, err
	}
	for i := uint64(0); i < nEntries; i++ {
		k, err := r.str()
		if err != nil {
			return nil, err
		}
		v, a := r.u64(), r.u64()
		if v >= uint64(len(code)) || a > maxFrameSlots {
			return nil, fmt.Errorf("ingagi: entry %q pc or arity out of range", k)
		}
		if _, exists := m.Tape.Entry[k]; exists {
			return nil, fmt.Errorf("ingagi: duplicate entry %q", k)
		}
		m.Tape.Entry[k] = int(v)
		m.Params[k] = int(a)
	}
	globals, err := r.readValueMap()
	if err != nil {
		return nil, err
	}
	nTypes, err := r.count(aotMaxEntries, "type")
	if err != nil {
		return nil, err
	}
	defs := map[string]map[string]Value{}
	for i := uint64(0); i < nTypes; i++ {
		head, err := r.strs()
		if err != nil {
			return nil, err
		}
		if len(head) != 2 {
			return nil, fmt.Errorf("ingagi: image type record %d has %d fields, want 2 (name, kind)", i, len(head))
		}
		if _, exists := m.Types[head[0]]; exists {
			return nil, fmt.Errorf("ingagi: duplicate type %q", head[0])
		}
		fields, err := r.strs()
		if err != nil {
			return nil, err
		}
		variants, err := r.strs()
		if err != nil {
			return nil, err
		}
		nm, err := r.count(aotMaxItems, "method")
		if err != nil {
			return nil, err
		}
		methods := map[string]string{}
		for j := uint64(0); j < nm; j++ {
			kv, err := r.strs()
			if err != nil {
				return nil, err
			}
			if len(kv) != 2 {
				return nil, fmt.Errorf("ingagi: image method record %d has %d fields, want 2", j, len(kv))
			}
			if _, exists := methods[kv[0]]; exists {
				return nil, fmt.Errorf("ingagi: duplicate method %q", kv[0])
			}
			methods[kv[0]] = kv[1]
		}
		ti := &TypeInfo{Kind: head[1], Defaults: map[string]Value{}, Methods: methods, Variants: variants}
		for _, f := range fields {
			ti.Fields = append(ti.Fields, TypeField{Name: f})
		}
		m.Types[head[0]] = ti
		if defs[head[0]], err = r.readValueMap(); err != nil {
			return nil, err
		}
	}
	nHooks, err := r.count(aotMaxEntries, "hook")
	if err != nil {
		return nil, err
	}
	for i := uint64(0); i < nHooks; i++ {
		fields := make([]string, 4)
		for j := range fields {
			if fields[j], err = r.str(); err != nil {
				return nil, err
			}
		}
		m.Hooks = append(m.Hooks, HookInfo{System: fields[0], Kind: fields[1], Entry: fields[2], Event: fields[3]})
	}
	if m.SystemNames, err = r.strs(); err != nil {
		return nil, err
	}
	prefabs, err := r.strs()
	if err != nil {
		return nil, err
	}
	for _, p := range prefabs {
		m.Entities[p] = nil // marker: prefab entry exists on tape
	}
	if m.Shaders, err = r.strs(); err != nil {
		return nil, err
	}
	if m.Pipelines, err = r.strs(); err != nil {
		return nil, err
	}
	if m.Events, err = r.strs(); err != nil {
		return nil, err
	}
	if r.err != nil {
		return nil, r.err
	}
	if r.remaining() != 0 {
		return nil, fmt.Errorf("ingagi: image has %d trailing bytes", r.remaining())
	}
	// Reject a tape the VM could not execute before anything is installed.
	if err := Validate(m); err != nil {
		return nil, fmt.Errorf("ingagi: invalid image: %w", err)
	}
	return &Decoded{Module: m, Globals: globals, Defs: defs}, nil
}

// readValue reads one tagged value, bounding nesting depth.
func (r *aotReader) readValue() (Value, error) {
	if r.err != nil {
		return NilValue, r.err
	}
	if r.depth >= aotMaxDepth {
		return NilValue, fmt.Errorf("ingagi: image value nests deeper than %d", aotMaxDepth)
	}
	r.depth++
	defer func() { r.depth-- }()
	switch tag := int(r.byte()); tag {
	case vNil:
		return NilValue, nil
	case vBool:
		b := r.byte()
		if b > 1 {
			return NilValue, fmt.Errorf("ingagi: invalid boolean byte %d", b)
		}
		return BoolValue(b != 0), r.err
	case vInt:
		return IntValue(r.i64()), nil
	case vFloat:
		return FloatValue(math.Float64frombits(r.u64())), nil
	case vString:
		s, err := r.str()
		if err != nil {
			return NilValue, err
		}
		return StringValue(s), nil
	case vVec2, vVec3, vVec4, vQuat:
		var v [4]float64
		for i := range v {
			v[i] = math.Float64frombits(r.u64())
		}
		k := KVec4
		switch tag {
		case vVec2:
			k = KVec2
		case vVec3:
			k = KVec3
		case vQuat:
			k = KQuat
		}
		return Value{K: k, V: v}, nil
	case vMat4:
		var m Mat4
		for i := range m {
			m[i] = math.Float64frombits(r.u64())
		}
		return Value{K: KMat4, O: m}, nil
	case vArray:
		n, err := r.count(aotMaxItems, "array")
		if err != nil {
			return NilValue, err
		}
		elems := make([]Value, 0, n)
		for i := uint64(0); i < n; i++ {
			e, err := r.readValue()
			if err != nil {
				return NilValue, err
			}
			elems = append(elems, e)
		}
		return Value{K: KArray, O: &ArrayObj{Elems: elems}}, nil
	case vMap:
		n, err := r.count(aotMaxItems, "map")
		if err != nil {
			return NilValue, err
		}
		m := make(map[string]Value, n)
		for i := uint64(0); i < n; i++ {
			k, err := r.str()
			if err != nil {
				return NilValue, err
			}
			e, err := r.readValue()
			if err != nil {
				return NilValue, err
			}
			m[k] = e
		}
		return Value{K: KMap, O: &MapObj{Fields: m}}, nil
	case vStruct:
		tn, err := r.str()
		if err != nil {
			return NilValue, err
		}
		n, err := r.count(aotMaxItems, "struct")
		if err != nil {
			return NilValue, err
		}
		m := make(map[string]Value, n)
		for i := uint64(0); i < n; i++ {
			k, err := r.str()
			if err != nil {
				return NilValue, err
			}
			e, err := r.readValue()
			if err != nil {
				return NilValue, err
			}
			m[k] = e
		}
		return Value{K: KStruct, O: &StructObj{Type: tn, Fields: m}}, nil
	case vEnum:
		tn, err := r.str()
		if err != nil {
			return NilValue, err
		}
		vn, err := r.str()
		if err != nil {
			return NilValue, err
		}
		n, err := r.count(aotMaxItems, "enum payload")
		if err != nil {
			return NilValue, err
		}
		var p []Value
		for i := uint64(0); i < n; i++ {
			e, err := r.readValue()
			if err != nil {
				return NilValue, err
			}
			p = append(p, e)
		}
		return Value{K: KEnum, O: &EnumObj{Type: tn, Variant: vn, Payload: p}}, nil
	case vClosure:
		s, err := r.str()
		if err != nil {
			return NilValue, err
		}
		return Value{K: KClosure, O: &ClosureObj{Name: s}}, nil
	case vBuiltin:
		s, err := r.str()
		if err != nil {
			return NilValue, err
		}
		return Value{K: KBuiltin, O: &BuiltinObj{Name: s}}, nil
	case vEntity:
		index, gen := r.u64(), r.u64()
		if index > 0xffffffff || gen > 0xffffffff {
			return NilValue, fmt.Errorf("ingagi: image entity reference is out of range")
		}
		return Value{K: KEntity, O: EntityRef{Index: uint32(index), Gen: uint32(gen)}}, nil
	default:
		return NilValue, fmt.Errorf("ingagi: bad value tag %d", tag)
	}
}

func (r *aotReader) readValueMap() (map[string]Value, error) {
	n, err := r.count(aotMaxEntries, "value map")
	if err != nil {
		return nil, err
	}
	m := make(map[string]Value, n)
	for i := uint64(0); i < n; i++ {
		k, err := r.str()
		if err != nil {
			return nil, err
		}
		if _, exists := m[k]; exists {
			return nil, fmt.Errorf("ingagi: duplicate value key %q", k)
		}
		v, err := r.readValue()
		if err != nil {
			return nil, err
		}
		m[k] = v
	}
	return m, r.err
}

// LoadDecoded installs a decoded image without running <init>.
func (vm *VM) LoadDecoded(d *Decoded) error {
	if d == nil {
		return fmt.Errorf("ingagi: nil image")
	}
	// Decode already validated; re-check so hand-built Decoded values cannot
	// install an unexecutable tape.
	if err := Validate(d.Module); err != nil {
		return err
	}
	m := d.Module
	vm.tape = m.Tape
	vm.funcs = map[string]int{}
	vm.params = map[string]int{}
	vm.caches = map[int]*inlineCache{}
	vm.profiles = map[int]*opProfile{}
	vm.methods = map[string]string{}
	vm.types = m.Types
	vm.defs = d.Defs
	vm.systems = map[string]map[string]int{}
	vm.evHooks = map[string][]int{}
	vm.hooksArg = map[int]string{}
	vm.observers = nil
	vm.events = nil
	vm.bindings = nil
	vm.prefabs = map[string]bool{}
	vm.globals = map[string]Value{}
	for k, v := range d.Globals {
		vm.globals[k] = v
	}
	for name, pc := range m.Tape.Entry {
		vm.funcs[name] = pc
	}
	for name, n := range m.Params {
		vm.params[name] = n
	}
	for name, ti := range m.Types {
		for method, entry := range ti.Methods {
			vm.methods[name+"::"+method] = entry
		}
	}
	for name := range m.Entities {
		vm.prefabs[name] = true
	}
	vm.rememberModule(m)
	for _, h := range m.Hooks {
		pc, ok := vm.funcs[h.Entry]
		if !ok {
			continue
		}
		if vm.systems[h.System] == nil {
			vm.systems[h.System] = map[string]int{}
		}
		vm.systems[h.System][h.Kind] = pc
		if h.Kind == "on_event" && h.Event != "" {
			vm.evHooks[h.Event] = append(vm.evHooks[h.Event], pc)
			vm.hooksArg[pc] = h.Event
		}
	}
	return nil
}

// rememberModule stashes AOT-relevant module facts for Encode.
func (vm *VM) rememberModule(m *Module) {
	vm.aotHooks = m.Hooks
	vm.aotSystems = m.SystemNames
	vm.aotPrefabs = []string{}
	for name := range m.Entities {
		vm.aotPrefabs = append(vm.aotPrefabs, name)
	}
	sort.Strings(vm.aotPrefabs)
	vm.aotStrings = map[string][]string{
		"shaders":   m.Shaders,
		"pipelines": m.Pipelines,
		"events":    m.Events,
	}
}

func sortedKeys[V any](m map[string]V) []string {
	keys := make([]string, 0, len(m))
	for key := range m {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}
