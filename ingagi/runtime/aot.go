package runtime

import (
	"bytes"
	"encoding/binary"
	"fmt"
	"math"
	"sort"
)

// AOT: ahead-of-time serialization of compiled modules.
//
// Encode snapshots a LOADED vm's executable state — tape, pool, entries,
// arities, globals and materialized field defaults — to a versioned binary
// image. Decode restores an equivalent module without running <init> or
// needing the compiler, which is the deployment path for shipped game
// scripts. Entity prefabs need no special casing: they compiled to
// `prefab::` entries like everything else.

const (
	aotMagic   = "IGBC1\x00"
	aotVersion = 1
)

type aotWriter struct {
	b   bytes.Buffer
	err error
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
	for k, v := range vm.funcs {
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
	for name, ti := range vm.types {
		w.strs([]string{name, ti.Kind})
		fields := make([]string, len(ti.Fields))
		for i, f := range ti.Fields {
			fields[i] = f.Name
		}
		w.strs(fields)
		w.strs(ti.Variants)
		w.u64(uint64(len(ti.Methods)))
		for k, v := range ti.Methods {
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
func (vm *VM) moduleHooks() []HookInfo      { return vm.aotHooks }
func (vm *VM) moduleSystems() []string      { return vm.aotSystems }
func (vm *VM) modulePrefabs() []string      { return vm.aotPrefabs }
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
		w.str(v.O.(*ClosureObj).Name)
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

type aotReader struct {
	b   *bytes.Reader
	err error
}

func (r *aotReader) u64() uint64 {
	var v uint64
	if r.err == nil {
		r.err = binary.Read(r.b, binary.LittleEndian, &v)
	}
	return v
}

func (r *aotReader) i64() int64 {
	var v int64
	if r.err == nil {
		r.err = binary.Read(r.b, binary.LittleEndian, &v)
	}
	return v
}

func (r *aotReader) str() string {
	n := r.u64()
	if r.err != nil {
		return ""
	}
	buf := make([]byte, n)
	if _, r.err = r.b.Read(buf); r.err != nil {
		return ""
	}
	return string(buf)
}

func (r *aotReader) strs() []string {
	n := r.u64()
	out := make([]string, 0, n)
	for i := uint64(0); i < n; i++ {
		out = append(out, r.str())
	}
	return out
}

// Decoded is an AOT image ready to load without the compiler.
type Decoded struct {
	Module  *Module
	Globals map[string]Value
	Defs    map[string]map[string]Value
}

// Decode parses an image produced by Encode.
func Decode(data []byte) (*Decoded, error) {
	r := &aotReader{b: bytes.NewReader(data)}
	magic := make([]byte, len(aotMagic))
	if _, err := r.b.Read(magic); err != nil || string(magic) != aotMagic {
		return nil, fmt.Errorf("ingagi: bad image magic")
	}
	if v := r.u64(); v != aotVersion {
		return nil, fmt.Errorf("ingagi: image version %d, want %d", v, aotVersion)
	}
	pool := NewConstPool()
	for n, i := r.u64(), uint64(0); i < n; i++ {
		pool.Ints = append(pool.Ints, r.i64())
	}
	for n, i := r.u64(), uint64(0); i < n; i++ {
		pool.Floats = append(pool.Floats, math.Float64frombits(r.u64()))
	}
	pool.Strings = r.strs()
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
	var code []Instr
	for n, i := r.u64(), uint64(0); i < n; i++ {
		op, _ := r.b.ReadByte()
		code = append(code, Instr{Op: Op(op), A: int32(r.i64()), B: int32(r.i64()), C: int32(r.i64())})
	}
	m := &Module{
		Tape:    &Tape{Code: code, Pool: pool, Entry: map[string]int{}},
		Types:   map[string]*TypeInfo{},
		Params:  map[string]int{},
		Resources: map[string]Value{},
	}
	for n, i := r.u64(), uint64(0); i < n; i++ {
		k := r.str()
		v := r.u64()
		a := r.u64()
		m.Tape.Entry[k] = int(v)
		m.Params[k] = int(a)
	}
	globals, err := r.readValueMap()
	if err != nil {
		return nil, err
	}
	defs := map[string]map[string]Value{}
	for n, i := r.u64(), uint64(0); i < n; i++ {
		head := r.strs() // name, kind
		fields := r.strs()
		variants := r.strs()
		nm := r.u64()
		methods := map[string]string{}
		for j := uint64(0); j < nm; j++ {
			kv := r.strs()
			if len(kv) == 2 {
				methods[kv[0]] = kv[1]
			}
		}
		ti := &TypeInfo{Kind: head[1], Defaults: map[string]Value{}, Methods: methods, Variants: variants}
		for _, f := range fields {
			ti.Fields = append(ti.Fields, TypeField{Name: f})
		}
		m.Types[head[0]] = ti
		d, err := r.readValueMap()
		if err != nil {
			return nil, err
		}
		defs[head[0]] = d
	}
	nh := r.u64()
	for i := uint64(0); i < nh; i++ {
		sys, kind, entry, ev := r.str(), r.str(), r.str(), r.str()
		m.Hooks = append(m.Hooks, HookInfo{System: sys, Kind: kind, Entry: entry, Event: ev})
	}
	m.SystemNames = r.strs()
	prefabs := r.strs()
	for _, p := range prefabs {
		m.Entities[p] = nil // marker: prefab entry exists on tape
	}
	m.Shaders = r.strs()
	m.Pipelines = r.strs()
	m.Events = r.strs()
	if r.err != nil {
		return nil, r.err
	}
	return &Decoded{Module: m, Globals: globals, Defs: defs}, nil
}

func (r *aotReader) readValue() (Value, error) {
	tag := vNil
	if b, err := r.b.ReadByte(); err != nil {
		return NilValue, err
	} else {
		tag = int(b)
	}
	switch tag {
	case vNil:
		return NilValue, nil
	case vBool:
		b, _ := r.b.ReadByte()
		return BoolValue(b != 0), nil
	case vInt:
		return IntValue(r.i64()), nil
	case vFloat:
		return FloatValue(math.Float64frombits(r.u64())), nil
	case vString:
		return StringValue(r.str()), nil
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
		n := r.u64()
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
		n := r.u64()
		m := map[string]Value{}
		for i := uint64(0); i < n; i++ {
			k := r.str()
			e, err := r.readValue()
			if err != nil {
				return NilValue, err
			}
			m[k] = e
		}
		return Value{K: KMap, O: &MapObj{Fields: m}}, nil
	case vStruct:
		tn := r.str()
		n := r.u64()
		m := map[string]Value{}
		for i := uint64(0); i < n; i++ {
			k := r.str()
			e, err := r.readValue()
			if err != nil {
				return NilValue, err
			}
			m[k] = e
		}
		return Value{K: KStruct, O: &StructObj{Type: tn, Fields: m}}, nil
	case vEnum:
		tn, vn := r.str(), r.str()
		n := r.u64()
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
		return Value{K: KClosure, O: &ClosureObj{Name: r.str()}}, nil
	case vBuiltin:
		return Value{K: KBuiltin, O: &BuiltinObj{Name: r.str()}}, nil
	case vEntity:
		return Value{K: KEntity, O: EntityRef{Index: uint32(r.u64()), Gen: uint32(r.u64())}}, nil
	}
	return NilValue, fmt.Errorf("ingagi: bad value tag %d", tag)
}

func (r *aotReader) readValueMap() (map[string]Value, error) {
	n := r.u64()
	m := map[string]Value{}
	for i := uint64(0); i < n; i++ {
		k := r.str()
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
	m := d.Module
	vm.tape = m.Tape
	vm.funcs = map[string]int{}
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
