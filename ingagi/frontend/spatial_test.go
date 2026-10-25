package frontend

import "testing"

func TestSpatialContextualNames(t *testing.T) {
	for _, src := range []string{
		`fn main() { let create=mesh; let inspect=type; return inspect(create([vec3(0,0,0),vec3(1,0,0),vec3(0,1,0)],[[0,1,2]])); }`,
		`fn main() { return mesh::make(); }`,
		`type Shape=mesh; fn main(m:Shape)->aabb { return mesh_bounds(m); }`,
		`shader S { mesh { } }`,
	} {
		if p := Parse(src); !p.OK() {
			t.Fatalf("%s: %v", src, p.Errors)
		}
	}
}
