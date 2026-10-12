package runtime

import "fmt"

// prepareImageValues validates image representations and copies aggregate
// storage before rebinding callable values. Loading the same Decoded into two
// VMs must never rewrite the other VM's callback bindings.
func prepareImageValues(d *Decoded, vm *VM) (map[string]Value, map[string]map[string]Value, error) {
	memo := map[any]Value{}
	active := map[any]bool{}
	var value func(Value, int) (Value, error)
	var fields func(map[string]Value, int) (map[string]Value, error)
	fields = func(src map[string]Value, depth int) (map[string]Value, error) {
		out := make(map[string]Value, len(src))
		for _, k := range sortedKeys(src) {
			v, err := value(src[k], depth)
			if err != nil {
				return nil, fmt.Errorf("value %q: %w", k, err)
			}
			out[k] = v
		}
		return out, nil
	}
	value = func(v Value, depth int) (Value, error) {
		if depth >= aotMaxDepth {
			return NilValue, fmt.Errorf("ingagi: cyclic or deeply nested image value")
		}
		bad := func() (Value, error) {
			return NilValue, fmt.Errorf("ingagi: invalid image value representation for %s", v.K)
		}
		var key any
		switch v.K {
		case KArray:
			p, ok := v.O.(*ArrayObj)
			if !ok || p == nil {
				return bad()
			}
			key = p
		case KMap:
			p, ok := v.O.(*MapObj)
			if !ok || p == nil {
				return bad()
			}
			key = p
		case KStruct:
			p, ok := v.O.(*StructObj)
			if !ok || p == nil {
				return bad()
			}
			key = p
		case KEnum:
			p, ok := v.O.(*EnumObj)
			if !ok || p == nil {
				return bad()
			}
			key = p
		case KClosure:
			p, ok := v.O.(*ClosureObj)
			if !ok || p == nil {
				return bad()
			}
			key = p
		case KBuiltin:
			p, ok := v.O.(*BuiltinObj)
			if !ok || p == nil {
				return bad()
			}
			key = p
		}
		if key != nil {
			if active[key] {
				return NilValue, fmt.Errorf("ingagi: cyclic image value")
			}
			if previous, ok := memo[key]; ok {
				return previous, nil
			}
			active[key] = true
			defer delete(active, key)
		}
		switch v.K {
		case KNil, KBool, KInt, KFloat, KString, KVec2, KVec3, KVec4, KQuat:
		case KMat4:
			if _, ok := v.O.(Mat4); !ok {
				return bad()
			}
		case KEntity:
			if _, ok := v.O.(EntityRef); !ok {
				return bad()
			}
		case KArray:
			p := v.O.(*ArrayObj)
			out := &ArrayObj{Elems: make([]Value, len(p.Elems))}
			for i, elem := range p.Elems {
				x, err := value(elem, depth+1)
				if err != nil {
					return NilValue, err
				}
				out.Elems[i] = x
			}
			v.O = out
		case KMap:
			out, err := fields(v.O.(*MapObj).Fields, depth+1)
			if err != nil {
				return NilValue, err
			}
			v.O = &MapObj{Fields: out}
		case KStruct:
			p := v.O.(*StructObj)
			// Dynamic host/component payload types may have no local descriptor.
			if p.Type == "" {
				return bad()
			}
			if ti := d.Module.Types[p.Type]; ti != nil && ti.Kind == "enum" {
				return bad()
			}
			out, err := fields(p.Fields, depth+1)
			if err != nil {
				return NilValue, err
			}
			v.O = &StructObj{Type: p.Type, Fields: out}
		case KEnum:
			p := v.O.(*EnumObj)
			ti := d.Module.Types[p.Type]
			if ti == nil || ti.Kind != "enum" {
				return bad()
			}
			found := false
			for _, name := range ti.Variants {
				if name == p.Variant {
					found = true
				}
			}
			if !found {
				return bad()
			}
			out := &EnumObj{Type: p.Type, Variant: p.Variant, Payload: make([]Value, len(p.Payload))}
			for i, elem := range p.Payload {
				x, err := value(elem, depth+1)
				if err != nil {
					return NilValue, err
				}
				out.Payload[i] = x
			}
			v.O = out
		case KClosure:
			p := v.O.(*ClosureObj)
			if _, ok := d.Module.Tape.Entry[p.Name]; !ok {
				return NilValue, fmt.Errorf("ingagi: closure references missing entry %q", p.Name)
			}
			if len(p.Up) != 0 {
				return NilValue, fmt.Errorf("ingagi: image v1 cannot restore captured closure %q", p.Name)
			}
			v.O = &ClosureObj{Name: p.Name}
		case KBuiltin:
			p := v.O.(*BuiltinObj)
			if p.Name == "" {
				return bad()
			}
			out := &BuiltinObj{Name: p.Name}
			if vm != nil {
				out.Fn = vm.builtins[p.Name]
				if out.Fn == nil {
					out.Fn = vm.hostFns[p.Name]
				}
				if out.Fn == nil {
					return NilValue, fmt.Errorf("ingagi: image requires unregistered builtin %q", p.Name)
				}
			}
			v.O = out
		default:
			return bad()
		}
		if key != nil {
			memo[key] = v
		}
		return v, nil
	}
	globals, err := fields(d.Globals, 0)
	if err != nil {
		return nil, nil, err
	}
	defs := make(map[string]map[string]Value, len(d.Defs))
	for _, name := range sortedKeys(d.Defs) {
		ti := d.Module.Types[name]
		if ti == nil {
			return nil, nil, fmt.Errorf("ingagi: defaults reference missing type %q", name)
		}
		known := map[string]bool{}
		for _, field := range ti.Fields {
			known[field.Name] = true
		}
		for field := range d.Defs[name] {
			if !known[field] {
				return nil, nil, fmt.Errorf("ingagi: default for unknown field %s.%s", name, field)
			}
		}
		defs[name], err = fields(d.Defs[name], 0)
		if err != nil {
			return nil, nil, err
		}
	}
	return globals, defs, nil
}
