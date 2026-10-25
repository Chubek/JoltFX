package sema

import "testing"

func TestSpatialTypes(t *testing.T) {
	for _, src := range []string{
		`fn f(m:mesh, v:vec3) -> aabb { let t:mat4=mat4_translate(mat4(1),v); return mesh_bounds(mesh_transform(m,t)); }`,
		`fn f(a:mat4,b:mat4,v:vec4) -> vec4 { return (inverse(a)*b)*v; }`,
		`fn f(v:vec2) -> float { return dot(v.normalize(),v)+length(v); }`,
		`fn f() -> mesh { return mesh([vec3(0,0,0),vec3(1,0,0),vec3(0,1,0)],[[0,1,2]]); }`,
		`fn f(v:vec3) -> vec3 { var x=v; x *= 2; return -x; }`,
		`fn f(m:mesh) -> float { return m.vertices[0].x; }`,
		`fn inverse(x:i64)->i64{return x;} fn f()->i64{return inverse(1);}`,
		`fn f() { let inverse=|x:i64| x; return inverse(1); }`,
	} {
		f := parseTest(t, src)
		if ds := CheckType(f, BuildRegistry(f)); len(ds) != 0 {
			t.Fatalf("%s: %v", src, ds)
		}
		if ds := CheckScope(f, BuildRegistry(f)); len(ds) != 0 {
			t.Fatalf("scope %s: %v", src, ds)
		}
	}
	for _, src := range []string{
		`fn f() { return vec2(1,2)+vec3(1,2,3); }`,
		`fn f() { return dot(vec2(1,2),vec4(1,2,3,4)); }`,
		`fn f() { return mat4(1)*vec3(1,2,3); }`,
		`fn f() { let m:mesh=vec3(1,2,3); }`,
		`fn f() { return vec2(1,2).z; }`,
		`fn f() { return inverse(3); }`,
		`fn f() { return mat4(1,2); }`,
		`fn f() { return contains(mesh_cube(1),vec3(0,0,0)); }`,
		`fn f() { return mesh_transform(mesh_cube(1),vec4(1,2,3,4)); }`,
		`fn f() { return vec3(1,2); }`,
		`fn f(v:vec3) { var a=v; a %= v; }`,
		`fn f(v:vec3) { return v < v; }`,
	} {
		f := parseTest(t, src)
		if ds := CheckType(f, BuildRegistry(f)); len(ds) == 0 {
			t.Fatalf("accepted %s", src)
		}
	}
}
