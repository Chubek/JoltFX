package runtime

import (
	"bytes"
	"github.com/Chubek/JoltFX/ingagi/geometry"
	"math"
	"testing"
)

func TestSpatialLanguage(t *testing.T) {
	v, _ := runSrc(t, `
fn moved(m: mesh, t: mat4) -> mesh { return mesh_transform(m, t); }
fn main() {
 let m: mesh = mesh_cube(2);
 let t: mat4 = mat4_translate(mat4(1), vec3(3, 0, 0));
 let n = moved(m, t);
 assert(type(m) == "mesh");
 assert(type(mesh_bounds(n)) == "aabb");
 assert(mesh_bounds(n).min == vec3(2, -1, -1));
 assert(contains(mesh_bounds(n), vec3(3, 0, 0)));
 assert(!contains(mesh_bounds(n), vec3(0, 0, 0)));
 assert(mesh_area(m) == 24);
 assert(mesh_area(mesh_transform(m, mat4_scale(mat4(1), vec3(2, 2, 2)))) == 96);
 assert(m.vertices[0] == vec3(-1, -1, -1));
 assert(mesh(m.vertices, m.triangles) == m);
 assert(dot(vec4(1, 2, 3, 4), vec4(1, 1, 1, 1)) == 10);
 assert(normalize(vec2(3, 4)) == vec2(0.6, 0.8));
 assert(length(vec4(0, 0, 0, 5)) == 5);
 assert((-vec2(1, 2))[1] == -2);
 assert((inverse(t) * t) == mat4(1));
 assert(transpose(t)[0] == vec4(1, 0, 0, 3));
 assert(determinant(mat4_scale(mat4(1), vec3(2, 3, 4))) == 24);
 assert((mat4(1) + mat4(1)) == mat4(2));
 assert((2 * mat4(1)) == mat4(2));
 assert((quat(0, 0, 0, 1) * vec3(1, 2, 3)) == vec3(1, 2, 3));
 assert((quat(0, 0, 0, 1) * quat(0, 0, 0, 1)) == quat(0, 0, 0, 1));
 return mesh_area(n);
}`, "main")
	if v.F != 24 {
		t.Fatal(v)
	}
}

func TestSpatialErrors(t *testing.T) {
	vm := New()
	for _, tc := range []struct {
		name string
		args []Value
	}{
		{"mesh", nil}, {"mesh", []Value{{K: KArray}, {K: KArray}}},
		{"mesh_area", []Value{{K: KMesh}}}, {"mesh_bounds", []Value{{K: KMesh}}},
		{"contains", []Value{{K: KAABB}, {K: KVec3}}},
		{"aabb", []Value{{K: KVec3, V: [4]float64{1}}, {K: KVec3}}},
		{"mat4", []Value{FloatValue(math.Inf(1))}},
		{"inverse", []Value{{K: KMat4, O: Mat4{}}}},
		{"inverse", []Value{{K: KMat4}}},
		{"dot", []Value{{K: KVec2}, {K: KVec3}}},
		{"normalize", []Value{{K: KVec4}}},
	} {
		fn := vm.builtins[tc.name]
		if fn == nil {
			t.Fatalf("missing builtin %s", tc.name)
		}
		if _, err := fn(vm, tc.args); err == nil {
			t.Errorf("accepted invalid %s", tc.name)
		}
	}
	if _, err := vm.arith(OpAdd, Value{K: KVec2}, Value{K: KVec3}, 0); err == nil {
		t.Fatal("mixed dimensions")
	}
}

func TestSpatialSnapshots(t *testing.T) {
	_, vm := runSrc(t, `const shape = mesh_cube(2); const box = mesh_bounds(shape); fn main() { return shape; }`, "main")
	image, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	d, err := Decode(image)
	if err != nil {
		t.Fatal(err)
	}
	other := New()
	if err = other.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	for _, name := range []string{"shape", "box"} {
		if !valueEqual(vm.globals[name], other.globals[name]) {
			t.Fatal(name)
		}
	}
	again, err := other.Encode()
	if err != nil || !bytes.Equal(image, again) {
		t.Fatalf("nondeterministic snapshot: %v", err)
	}
	vertices, err := other.loadField(other.globals["shape"], "vertices")
	if err != nil {
		t.Fatal(err)
	}
	vertices.O.(*ArrayObj).Elems[0] = Value{K: KVec3}
	if !valueEqual(vm.globals["shape"], other.globals["shape"]) {
		t.Fatal("mutable mesh view")
	}
}

func TestSpatialOwnershipAndFailureAtomicity(t *testing.T) {
	vm := New()
	m, _ := vm.builtins["mesh_cube"](vm, []Value{IntValue(2)})
	original, _ := valueMesh(m)
	for _, matrix := range []Mat4{
		IdentityMat4().Scale(Vec3{0, 0, 0}),
		Perspective(1, 1, 0.1, 10),
		IdentityMat4().Translate(Vec3{1e10, 0, 0}),
	} {
		if _, err := bMeshTransform(vm, []Value{m, {K: KMat4, O: matrix}}); err == nil {
			t.Fatal("invalid mesh transform accepted")
		}
		after, _ := valueMesh(m)
		if after.Vertices[0] != original.Vertices[0] {
			t.Fatal("failed transform mutated input")
		}
	}
	copy, _ := meshMapValue(original)
	fields := copy.O.(*MapObj).Fields
	owned, err := bMesh(vm, []Value{fields["vertices"], fields["triangles"]})
	if err != nil {
		t.Fatal(err)
	}
	fields["vertices"].O.(*ArrayObj).Elems[0] = Value{K: KVec3}
	fields["triangles"].O.(*ArrayObj).Elems[0].O.(*ArrayObj).Elems[0] = IntValue(-1)
	if !valueEqual(m, owned) {
		t.Fatal("mesh retained caller arrays")
	}
	if _, err := vm.storeField(m, NilValue, "vertices"); err == nil {
		t.Fatal("writable mesh")
	}
	for _, idx := range []Value{IntValue(-1), IntValue(4), FloatValue(0.5), FloatValue(math.NaN())} {
		if _, err := vm.loadIndex(Value{K: KMat4, O: IdentityMat4()}, idx); err == nil {
			t.Fatal("invalid matrix index")
		}
	}
}

func TestMalformedSpatialImageValues(t *testing.T) {
	_, vm := runSrc(t, `fn main() { return 0; }`, "main")
	image, _ := vm.Encode()
	for _, v := range []Value{
		{K: KMesh}, {K: KMesh, O: &meshObj{}},
		{K: KAABB}, {K: KAABB, O: geometry.Bounds{Min: geometry.Vec3{X: math.NaN()}}},
	} {
		d, err := Decode(image)
		if err != nil {
			t.Fatal(err)
		}
		d.Globals["bad"] = v
		if err = New().LoadDecoded(d); err == nil {
			t.Fatal("accepted invalid image value")
		}
		vm.globals["bad"] = v
		if _, err = vm.Encode(); err == nil {
			t.Fatal("encoded invalid spatial value")
		}
		delete(vm.globals, "bad")
	}
	// Older image versions cannot smuggle new value tags.
	for _, tag := range []byte{vMesh, vAABB} {
		r := &aotReader{b: bytes.NewReader([]byte{tag}), version: 2}
		if _, err := r.readValue(); err == nil {
			t.Fatal("v3 tag in v2 image")
		}
	}
}
