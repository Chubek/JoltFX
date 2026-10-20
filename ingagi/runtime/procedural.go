package runtime

import (
	"fmt"
	"math"

	"github.com/Chubek/JoltFX/ingagi/geometry"
	"github.com/Chubek/JoltFX/ingagi/noise"
)

// These builtins return ordinary serializable values, never unowned C handles.
// Their per-call loops/allocations are bounded; the VM budget is not a total
// memory or wall-clock sandbox (as with all existing host/builtin operations).
func registerProcedural(vm *VM) {
	vm.builtins["noise2"] = noiseBuiltin(2)
	vm.builtins["noise3"] = noiseBuiltin(3)
	vm.builtins["fbm3"] = fbmBuiltin
	vm.builtins["mesh_cube"] = meshPrimitive("cube")
	vm.builtins["mesh_plane"] = meshPrimitive("plane")
	vm.builtins["mesh_sphere"] = meshPrimitive("sphere")
	vm.builtins["mesh_normals"] = meshNormals
	vm.builtins["mesh_bounds"] = meshBounds
	vm.builtins["mesh_translate"] = meshTranslate
}
func proceduralNumber(v Value) (float64, error) {
	n, ok := v.AsFloat()
	if !ok || math.IsNaN(n) || math.IsInf(n, 0) {
		return 0, fmt.Errorf("procedural: expected a finite number")
	}
	return n, nil
}
func noiseBuiltin(dim int) func(*VM, []Value) (Value, error) {
	return func(vm *VM, args []Value) (Value, error) {
		if len(args) != dim && len(args) != dim+1 {
			return NilValue, vm.fail("noise%d takes %d coordinates and an optional integer seed", dim, dim)
		}
		var coords [3]float64
		for i := 0; i < dim; i++ {
			v, err := proceduralNumber(args[i])
			if err != nil {
				return NilValue, err
			}
			coords[i] = v
		}
		seed := uint64(0)
		if len(args) > dim {
			if args[dim].K != KInt {
				return NilValue, vm.fail("noise seed must be an integer")
			}
			seed = uint64(args[dim].I)
		}
		v, err := noise.New(seed).Sample3(coords[0], coords[1], coords[2])
		if err != nil {
			return NilValue, err
		}
		return FloatValue(v), nil
	}
}
func fbmBuiltin(vm *VM, args []Value) (Value, error) {
	if len(args) < 5 || len(args) > 8 {
		return NilValue, vm.fail("fbm3 takes (x,y,z,seed,octaves[,frequency,lacunarity,gain])")
	}
	if args[3].K != KInt || args[4].K != KInt || args[4].I < 1 || args[4].I > 32 {
		return NilValue, vm.fail("fbm3 requires an integer seed and 1..32 octaves")
	}
	var coords [3]float64
	for i := range coords {
		v, err := proceduralNumber(args[i])
		if err != nil {
			return NilValue, err
		}
		coords[i] = v
	}
	p := noise.DefaultProfile(uint64(args[3].I))
	p.Octaves = int(args[4].I)
	fields := []*float64{&p.Frequency, &p.Lacunarity, &p.Gain}
	for i := 5; i < len(args); i++ {
		n, err := proceduralNumber(args[i])
		if err != nil {
			return NilValue, err
		}
		*fields[i-5] = n
	}
	n, err := noise.NewProfile(p)
	if err != nil {
		return NilValue, err
	}
	v, err := n.Sample3(coords[0], coords[1], coords[2])
	if err != nil {
		return NilValue, err
	}
	return FloatValue(v), nil
}

const meshValueLimit = 65536 // shared snapshot/collection representation bound
func meshPrimitive(kind string) func(*VM, []Value) (Value, error) {
	return func(vm *VM, args []Value) (Value, error) {
		want := 1
		if kind == "plane" {
			want = 2
		}
		if kind == "sphere" {
			want = 3
		}
		if len(args) != want {
			return NilValue, vm.fail("mesh_%s takes %d arguments", kind, want)
		}
		size, err := proceduralNumber(args[0])
		if err != nil {
			return NilValue, err
		}
		var m geometry.Mesh
		switch kind {
		case "cube":
			m, err = geometry.Cube(size)
		case "plane":
			var depth float64
			depth, err = proceduralNumber(args[1])
			if err == nil {
				m, err = geometry.Plane(size, depth)
			}
		case "sphere":
			if args[1].K != KInt || args[2].K != KInt || args[1].I < 3 || args[2].I < 2 || args[2].I > meshValueLimit || args[1].I > int64(meshValueLimit/2)/(args[2].I-1) {
				return NilValue, vm.fail("mesh_sphere requires bounded integer segments/rings")
			}
			m, err = geometry.UVSphere(size, int(args[1].I), int(args[2].I))
		}
		if err != nil {
			return NilValue, err
		}
		return meshValue(m)
	}
}
func vectorValue(v geometry.Vec3) Value { return Value{K: KVec3, V: [4]float64{v.X, v.Y, v.Z, 0}} }
func meshValue(m geometry.Mesh) (Value, error) {
	if len(m.Vertices) > meshValueLimit || len(m.Triangles) > meshValueLimit {
		return NilValue, fmt.Errorf("procedural: mesh exceeds VM collection limit")
	}
	vertices := make([]Value, len(m.Vertices))
	triangles := make([]Value, len(m.Triangles))
	for i, v := range m.Vertices {
		vertices[i] = vectorValue(v)
	}
	for i, t := range m.Triangles {
		triangles[i] = Value{K: KArray, O: &ArrayObj{Elems: []Value{IntValue(int64(t[0])), IntValue(int64(t[1])), IntValue(int64(t[2]))}}}
	}
	return Value{K: KMap, O: &MapObj{Fields: map[string]Value{"vertices": {K: KArray, O: &ArrayObj{Elems: vertices}}, "triangles": {K: KArray, O: &ArrayObj{Elems: triangles}}}}}, nil
}
func valueMesh(v Value) (geometry.Mesh, error) {
	invalid := fmt.Errorf("procedural: expected {vertices: [vec3...], triangles: [[i,j,k]...]}")
	if v.K != KMap {
		return geometry.Mesh{}, invalid
	}
	obj, ok := v.O.(*MapObj)
	if !ok || obj == nil {
		return geometry.Mesh{}, invalid
	}
	vertices, triangles := obj.Fields["vertices"], obj.Fields["triangles"]
	if vertices.K != KArray || triangles.K != KArray {
		return geometry.Mesh{}, invalid
	}
	vs, ok := vertices.O.(*ArrayObj)
	if !ok || vs == nil {
		return geometry.Mesh{}, invalid
	}
	ts, ok := triangles.O.(*ArrayObj)
	if !ok || ts == nil {
		return geometry.Mesh{}, invalid
	}
	if len(vs.Elems) > meshValueLimit || len(ts.Elems) > meshValueLimit {
		return geometry.Mesh{}, invalid
	}
	m := geometry.Mesh{Vertices: make([]geometry.Vec3, len(vs.Elems)), Triangles: make([]geometry.Triangle, len(ts.Elems))}
	for i, v := range vs.Elems {
		if v.K != KVec3 {
			return geometry.Mesh{}, invalid
		}
		m.Vertices[i] = geometry.Vec3{X: v.V[0], Y: v.V[1], Z: v.V[2]}
	}
	for i, t := range ts.Elems {
		if t.K != KArray {
			return geometry.Mesh{}, invalid
		}
		a, ok := t.O.(*ArrayObj)
		if !ok || a == nil || len(a.Elems) != 3 {
			return geometry.Mesh{}, invalid
		}
		for j, k := range a.Elems {
			if k.K != KInt || k.I < 0 || k.I >= int64(len(m.Vertices)) {
				return geometry.Mesh{}, invalid
			}
			m.Triangles[i][j] = uint32(k.I)
		}
	}
	return m, m.Validate()
}
func meshNormals(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("mesh_normals takes one mesh")
	}
	m, err := valueMesh(args[0])
	if err != nil {
		return NilValue, err
	}
	n, err := m.VertexNormals()
	if err != nil {
		return NilValue, err
	}
	values := make([]Value, len(n))
	for i, v := range n {
		values[i] = vectorValue(v)
	}
	return Value{K: KArray, O: &ArrayObj{Elems: values}}, nil
}
func meshBounds(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("mesh_bounds takes one mesh")
	}
	m, err := valueMesh(args[0])
	if err != nil {
		return NilValue, err
	}
	b, err := m.Bounds()
	if err != nil {
		return NilValue, err
	}
	return Value{K: KMap, O: &MapObj{Fields: map[string]Value{"min": vectorValue(b.Min), "max": vectorValue(b.Max)}}}, nil
}
func meshTranslate(vm *VM, args []Value) (Value, error) {
	if len(args) != 2 || args[1].K != KVec3 {
		return NilValue, vm.fail("mesh_translate takes (mesh, vec3)")
	}
	m, err := valueMesh(args[0])
	if err != nil {
		return NilValue, err
	}
	delta := geometry.Vec3{X: args[1].V[0], Y: args[1].V[1], Z: args[1].V[2]}
	m, err = m.Transform(func(v geometry.Vec3) (geometry.Vec3, error) { return v.Add(delta), nil })
	if err != nil {
		return NilValue, err
	}
	return meshValue(m)
}
