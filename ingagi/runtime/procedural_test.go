package runtime

import (
	"math"
	"testing"

	"github.com/Chubek/JoltFX/ingagi/noise"
)

func TestProceduralScript(t *testing.T) {
	v, _ := runSrc(t, `fn main() {
 let cube = mesh_cube(2.0);
 let moved = mesh_translate(cube, vec3(3.0, 0.0, 0.0));
 let bounds = mesh_bounds(moved);
 let normals = mesh_normals(cube);
 assert(len(cube.vertices) == 8);
 assert(len(cube.triangles) == 12);
 assert(len(normals) == 8);
 assert(bounds.min.x == 2.0);
 assert(cube.vertices[0].x == -1.0);
 let sphere = mesh_sphere(1.0, 16, 8);
 assert(len(sphere.vertices) == 114);
 assert(len(mesh_plane(2.0, 3.0).triangles) == 2);
 return noise3(0.17, 0.29, 0.41, 9);
}`, "main")
	want, _ := noise.New(9).Sample3(.17, .29, .41)
	if v.K != KFloat || v.F != want {
		t.Fatalf("noise: %v != %g", v, want)
	}
	v, _ = runSrc(t, `fn main() { return fbm3(0.17,0.29,0.41,9,6); }`, "main")
	p := noise.DefaultProfile(9)
	p.Octaves = 6
	n, _ := noise.NewProfile(p)
	want, _ = n.Sample3(.17, .29, .41)
	if v.F != want {
		t.Fatal("fBm mismatch")
	}
}

func TestProceduralBuiltinErrors(t *testing.T) {
	vm := New()
	for _, test := range []struct {
		name string
		args []Value
	}{
		{"noise2", nil}, {"noise3", []Value{FloatValue(math.NaN()), IntValue(0), IntValue(0)}},
		{"noise2", []Value{IntValue(0), IntValue(0), FloatValue(1)}},
		{"fbm3", []Value{IntValue(0), IntValue(0), IntValue(0), IntValue(0), IntValue(33)}},
		{"mesh_cube", []Value{IntValue(-1)}}, {"mesh_sphere", []Value{IntValue(1), IntValue(65536), IntValue(65536)}},
		{"mesh_sphere", []Value{IntValue(1), FloatValue(16), IntValue(8)}},
		{"mesh_normals", []Value{Value{K: KMap}}}, {"mesh_bounds", []Value{IntValue(0)}},
		{"mesh_translate", []Value{IntValue(0), Value{K: KVec3}}},
	} {
		if _, err := vm.builtins[test.name](vm, test.args); err == nil {
			t.Fatalf("accepted invalid %s", test.name)
		}
	}
	// Failed calls must not poison subsequent budgeted script execution.
	v, _ := runSrc(t, `fn main() { return noise2(0.25,0.75,42); }`, "main")
	if v.K != KFloat {
		t.Fatal(v)
	}
}

func TestProceduralSnapshotValues(t *testing.T) {
	_, vm := runSrc(t, `fn main() { return 0; }`, "main")
	mesh, err := vm.builtins["mesh_sphere"](vm, []Value{FloatValue(1), IntValue(16), IntValue(8)})
	if err != nil {
		t.Fatal(err)
	}
	vm.globals["saved_mesh"] = mesh
	image, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	decoded, err := Decode(image)
	if err != nil {
		t.Fatal(err)
	}
	restored := New()
	if err = restored.LoadDecoded(decoded); err != nil {
		t.Fatal(err)
	}
	if _, err = valueMesh(restored.globals["saved_mesh"]); err != nil {
		t.Fatal(err)
	}
	// Snapshot installation must not alias the original script's owned arrays.
	vertices := restored.globals["saved_mesh"].O.(*meshObj)
	vertices.data.Vertices[0].X = 123
	original := mesh.O.(*meshObj)
	if original.data.Vertices[0].X == 123 {
		t.Fatal("snapshot mesh aliases original")
	}
}
