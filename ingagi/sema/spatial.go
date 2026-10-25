package sema

import (
	"fmt"
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"strings"
)

func vectorType(t string) bool { return t == "vec2" || t == "vec3" || t == "vec4" }
func numberType(t string) bool { return t == "int" || t == "float" }

func spatialType(t string) bool {
	return vectorType(t) || t == "quat" || t == "mat4" || t == "mesh" || t == "aabb"
}

// Spatial operator result types mirror the VM's value operations.
func spatialBinary(op, x, y string) string {
	if op == "+" || op == "-" || op == "*" || op == "/" {
		if vectorType(x) && (x == y || numberType(y)) {
			return x
		}
		if numberType(x) && vectorType(y) {
			return y
		}
	}
	if (op == "+" || op == "-" || op == "*") && x == "mat4" && y == "mat4" {
		return "mat4"
	}
	if op == "*" {
		if x == "mat4" && y == "vec4" {
			return "vec4"
		}
		if (x == "mat4" && numberType(y)) || (numberType(x) && y == "mat4") {
			return "mat4"
		}
		if x == "quat" && (y == "quat" || y == "vec3") {
			return y
		}
	}
	return ""
}

type spatialSignature struct {
	result string
	args   []string
}

var spatialSignatures = map[string]spatialSignature{
	"vec2":             {"vec2", []string{"float", "float"}},
	"vec3":             {"vec3", []string{"float", "float", "float"}},
	"vec4":             {"vec4", []string{"float", "float", "float", "float"}},
	"quat":             {"quat", []string{"float", "float", "float", "float"}},
	"mat4_identity":    {"mat4", nil},
	"mat4_translate":   {"mat4", []string{"mat4", "vec3"}},
	"mat4_scale":       {"mat4", []string{"mat4", "vec3"}},
	"mat4_rotate":      {"mat4", []string{"mat4", "float", "vec3"}},
	"mat4_perspective": {"mat4", []string{"float", "float", "float", "float"}},
	"mat4_lookat":      {"mat4", []string{"vec3", "vec3", "vec3"}},
	"transpose":        {"mat4", []string{"mat4"}},
	"inverse":          {"mat4", []string{"mat4"}},
	"determinant":      {"float", []string{"mat4"}},
	"cross":            {"vec3", []string{"vec3", "vec3"}},
	"mesh":             {"mesh", []string{"[vec3]", "[[int]]"}},
	"mesh_cube":        {"mesh", []string{"float"}},
	"mesh_plane":       {"mesh", []string{"float", "float"}},
	"mesh_sphere":      {"mesh", []string{"float", "int", "int"}},
	"mesh_bounds":      {"aabb", []string{"mesh"}},
	"mesh_area":        {"float", []string{"mesh"}},
	"mesh_normals":     {"[vec3]", []string{"mesh"}},
	"mesh_translate":   {"mesh", []string{"mesh", "vec3"}},
	"mesh_transform":   {"mesh", []string{"mesh", "mat4"}},
	"aabb":             {"aabb", []string{"vec3", "vec3"}},
	"contains":         {"bool", []string{"aabb", "vec3"}},
}

func spatialBuiltin(name string) bool {
	_, ok := spatialSignatures[name]
	return ok || name == "mat4" || name == "dot" || name == "normalize" || name == "length"
}

func (c *typeChecker) spatialCall(name string, args []string, sp fe.Span) (string, bool) {
	sig, ok := spatialSignatures[name]
	switch name {
	case "mat4":
		sig, ok = spatialSignature{result: "mat4"}, true
		switch len(args) {
		case 0:
		case 1:
			sig.args = []string{"float"}
		case 4:
			sig.args = []string{"vec4", "vec4", "vec4", "vec4"}
		default:
			c.error("E0201", "mat4 expects 0, 1 or 4 arguments", sp)
			return "mat4", true
		}
	case "dot", "normalize", "length":
		want := 1
		result := "float"
		if name == "dot" {
			want = 2
		}
		if len(args) != want {
			c.error("E0201", fmt.Sprintf("%s expects %d arguments", name, want), sp)
			return "?", true
		}
		if name == "normalize" {
			result = args[0]
		}
		for _, a := range args {
			if a != "?" && !vectorType(a) && !(name != "dot" && a == "quat") {
				c.error("E0201", name+" requires vectors", sp)
			}
		}
		if name == "dot" {
			c.expect(args[0], args[1], sp)
		}
		return result, true
	}
	if !ok {
		return "", false
	}
	if len(args) != len(sig.args) {
		c.error("E0201", fmt.Sprintf("%s expects %d arguments", name, len(sig.args)), sp)
	}
	for i, a := range args {
		if i < len(sig.args) {
			c.expect(sig.args[i], a, sp)
		}
	}
	return sig.result, true
}

func spatialField(t, name string) (string, bool) {
	if vectorType(t) || t == "quat" {
		n := 4
		if t == "vec2" {
			n = 2
		}
		if t == "vec3" {
			n = 3
		}
		for _, fields := range []string{"xyzw", "rgba"} {
			if len(name) == 1 && strings.Contains(fields[:n], name) {
				return "float", true
			}
		}
		return "", true
	}
	if t == "mesh" {
		if name == "vertices" {
			return "[vec3]", true
		}
		if name == "triangles" {
			return "[[int]]", true
		}
		return "", true
	}
	if t == "aabb" {
		if name == "min" || name == "max" {
			return "vec3", true
		}
		return "", true
	}
	return "", false
}
