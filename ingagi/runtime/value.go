// Value model for the Ingagi VM.
//
// Values are dynamically typed at runtime; the static type system lives in
// sema/. Component payloads cross into ecs/ as plain `any` (maps of field
// name to Value), so ecs never imports runtime and the engine can move
// component data without a Go dependency on the VM.
package runtime

import (
	"fmt"
	"strings"
)

// Kind identifies a runtime value's representation.
type Kind int

const (
	KNil Kind = iota
	KBool
	KInt
	KFloat
	KString
	KVec2
	KVec3
	KVec4
	KMat4
	KQuat
	KArray
	KMap
	KStruct
	KEnum
	KClosure
	KBuiltin
	KWorld
	KEntity
	KHandle // opaque host object (native extensions, resources)
)

func (k Kind) String() string {
	switch k {
	case KNil:
		return "nil"
	case KBool:
		return "bool"
	case KInt:
		return "int"
	case KFloat:
		return "float"
	case KString:
		return "string"
	case KVec2:
		return "vec2"
	case KVec3:
		return "vec3"
	case KVec4:
		return "vec4"
	case KMat4:
		return "mat4"
	case KQuat:
		return "quat"
	case KArray:
		return "array"
	case KMap:
		return "map"
	case KStruct:
		return "struct"
	case KEnum:
		return "enum"
	case KClosure:
		return "closure"
	case KBuiltin:
		return "builtin"
	case KWorld:
		return "world"
	case KEntity:
		return "entity"
	case KHandle:
		return "handle"
	}
	return "unknown"
}

// Value is one Ingagi runtime value.
type Value struct {
	K Kind
	B bool
	I int64
	F float64
	S string
	// V holds vec2/vec3/vec4 (V[0:2..4]) and quaternions (x,y,z,w).
	V [4]float64
	// M holds column-major 4x4 matrices (glm layout).
	M *[16]float64
	// O holds heap objects: *ArrayObj, *MapObj, *StructObj, *EnumObj,
	// *ClosureObj, *BuiltinObj, host handles, entity refs, world refs.
	O any
}

var NilValue = Value{K: KNil}

func BoolValue(b bool) Value     { return Value{K: KBool, B: b} }
func IntValue(i int64) Value     { return Value{K: KInt, I: i} }
func FloatValue(f float64) Value { return Value{K: KFloat, F: f} }
func StringValue(s string) Value { return Value{K: KString, S: s} }

// IsNil reports nil-ness.
func (v Value) IsNil() bool { return v.K == KNil }

// Truthy follows Ingagi semantics: nil/false/0/0.0/"" and empty
// arrays/maps are false; everything else is true.
func (v Value) Truthy() bool {
	switch v.K {
	case KNil:
		return false
	case KBool:
		return v.B
	case KInt:
		return v.I != 0
	case KFloat:
		return v.F != 0
	case KString:
		return v.S != ""
	case KArray:
		return len(v.O.(*ArrayObj).Elems) != 0
	case KMap:
		return len(v.O.(*MapObj).Fields) != 0
	default:
		return true
	}
}

// AsFloat widens ints to floats for arithmetic promotion.
func (v Value) AsFloat() (float64, bool) {
	switch v.K {
	case KFloat:
		return v.F, true
	case KInt:
		return float64(v.I), true
	}
	return 0, false
}

// String renders a value for diagnostics and `print`.
func (v Value) String() string {
	switch v.K {
	case KNil:
		return "null"
	case KBool:
		if v.B {
			return "true"
		}
		return "false"
	case KInt:
		return fmt.Sprintf("%d", v.I)
	case KFloat:
		return fmt.Sprintf("%g", v.F)
	case KString:
		return v.S
	case KVec2:
		return fmt.Sprintf("vec2(%g, %g)", v.V[0], v.V[1])
	case KVec3:
		return fmt.Sprintf("vec3(%g, %g, %g)", v.V[0], v.V[1], v.V[2])
	case KVec4:
		return fmt.Sprintf("vec4(%g, %g, %g, %g)", v.V[0], v.V[1], v.V[2], v.V[3])
	case KMat4:
		return "mat4(...)"
	case KQuat:
		return fmt.Sprintf("quat(%g, %g, %g, %g)", v.V[0], v.V[1], v.V[2], v.V[3])
	case KArray:
		var b strings.Builder
		b.WriteByte('[')
		for i, e := range v.O.(*ArrayObj).Elems {
			if i > 0 {
				b.WriteString(", ")
			}
			b.WriteString(e.String())
		}
		b.WriteByte(']')
		return b.String()
	case KMap:
		return "map(...)"
	case KStruct:
		s := v.O.(*StructObj)
		var b strings.Builder
		b.WriteString(s.Type + "{")
		i := 0
		for k, e := range s.Fields {
			if i > 0 {
				b.WriteString(", ")
			}
			fmt.Fprintf(&b, "%s: %s", k, e.String())
			i++
		}
		b.WriteByte('}')
		return b.String()
	case KEnum:
		e := v.O.(*EnumObj)
		if len(e.Payload) == 0 {
			return e.Variant
		}
		return fmt.Sprintf("%s(...)", e.Variant)
	case KClosure:
		return fmt.Sprintf("fn %s", v.O.(*ClosureObj).Name)
	case KBuiltin:
		return fmt.Sprintf("builtin %s", v.O.(*BuiltinObj).Name)
	case KWorld:
		return "world"
	case KEntity:
		e := v.O.(EntityRef)
		return fmt.Sprintf("entity(%d:%d)", e.Index, e.Gen)
	case KHandle:
		return fmt.Sprintf("handle(%T)", v.O)
	}
	return "?"
}

// ArrayObj is a heap array value.
type ArrayObj struct{ Elems []Value }

// MapObj is a heap map value (string keys).
type MapObj struct{ Fields map[string]Value }

// StructObj is a struct/component/entity-payload instance.
type StructObj struct {
	Type   string
	Fields map[string]Value
}

// EnumObj is an enum variant value.
type EnumObj struct {
	Type    string
	Variant string
	Payload []Value
}

// EntityRef identifies an ecs entity from script.
type EntityRef struct {
	Index uint32
	Gen   uint32
}

// ClosureObj is a user function activation template.
type ClosureObj struct {
	Name string
	Fn   int // code-tape entry; -1 for AST closures run by Call
	Up   []Value
}

// BuiltinObj is a host-provided function.
type BuiltinObj struct {
	Name string
	Fn   func(vm *VM, args []Value) (Value, error)
}
