package runtime

import (
	"fmt"

	"github.com/Chubek/JoltFX/ingagi/geometry"
)

// meshObj owns immutable storage. No operation exposes these slices to script.
type meshObj struct{ data geometry.Mesh }

func registerGeometry(vm *VM) {
	vm.builtins["mesh"] = bMesh
	vm.builtins["aabb"] = bAABB
	vm.builtins["contains"] = bContains
	vm.builtins["mesh_area"] = bMeshArea
	vm.builtins["mesh_transform"] = bMeshTransform
}

func bMesh(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("mesh takes (vertices, triangles)")
	}
	m, err := valueMesh(Value{K: KMap, O: &MapObj{Fields: map[string]Value{"vertices": args[0], "triangles": args[1]}}})
	if err != nil {
		return NilValue, err
	}
	return meshValue(m)
}

func finiteVec3(v Value) (geometry.Vec3, error) {
	if v.K != KVec3 {
		return geometry.Vec3{}, fmt.Errorf("geometry: expected vec3")
	}
	for _, x := range v.V[:3] {
		if _, err := proceduralNumber(FloatValue(x)); err != nil {
			return geometry.Vec3{}, err
		}
	}
	return geometry.Vec3{X: v.V[0], Y: v.V[1], Z: v.V[2]}, nil
}

func bAABB(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("aabb takes (min: vec3, max: vec3)")
	}
	return boundsValue(args[0], args[1])
}

func boundsValue(minimum, maximum Value) (Value, error) {
	lo, err := finiteVec3(minimum)
	if err != nil {
		return NilValue, err
	}
	hi, err := finiteVec3(maximum)
	if err != nil {
		return NilValue, err
	}
	b := geometry.Bounds{Min: lo, Max: hi}
	if err = b.Validate(); err != nil {
		return NilValue, err
	}
	return Value{K: KAABB, O: b}, nil
}

func valueBounds(v Value) (geometry.Bounds, error) {
	b, ok := v.O.(geometry.Bounds)
	if v.K != KAABB || !ok {
		return geometry.Bounds{}, fmt.Errorf("geometry: expected aabb")
	}
	return b, b.Validate()
}

func bContains(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("contains takes (aabb, vec3)")
	}
	b, err := valueBounds(args[0])
	if err != nil {
		return NilValue, err
	}
	p, err := finiteVec3(args[1])
	if err != nil {
		return NilValue, err
	}
	return BoolValue(b.Contains(p)), nil
}

func bMeshArea(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("mesh_area takes one mesh")
	}
	m, err := valueMesh(args[0])
	if err != nil {
		return NilValue, err
	}
	a, err := m.Area()
	if err != nil {
		return NilValue, err
	}
	return FloatValue(a), nil
}

func bMeshTransform(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 {
		return NilValue, vm.fail("mesh_transform takes (mesh, mat4)")
	}
	m, err := valueMesh(args[0])
	if err != nil {
		return NilValue, err
	}
	t, err := finiteMat(args[1])
	if err != nil {
		return NilValue, err
	}
	if t[3] != 0 || t[7] != 0 || t[11] != 0 || t[15] != 1 {
		return NilValue, vm.fail("mesh_transform requires an affine matrix")
	}
	out, err := m.Transform(func(v geometry.Vec3) (geometry.Vec3, error) {
		r := t.MulVec4(Vec4{v.X, v.Y, v.Z, 1})
		return geometry.Vec3{X: r.X, Y: r.Y, Z: r.Z}, nil
	})
	if err != nil {
		return NilValue, err
	}
	return meshValue(out)
}

func geometryField(obj Value, name string) (Value, error) {
	if obj.K == KMesh {
		m, err := valueMesh(obj)
		if err != nil {
			return NilValue, err
		}
		if name == "vertices" || name == "triangles" {
			v, err := meshMapValue(m)
			if err != nil {
				return NilValue, err
			}
			return v.O.(*MapObj).Fields[name], nil
		}
	} else {
		b, err := valueBounds(obj)
		if err != nil {
			return NilValue, err
		}
		if name == "min" {
			return vectorValue(b.Min), nil
		}
		if name == "max" {
			return vectorValue(b.Max), nil
		}
	}
	return NilValue, fmt.Errorf("geometry: no field %q on %s", name, obj.K)
}
