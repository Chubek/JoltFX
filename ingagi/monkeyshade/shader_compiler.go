// Package shader lowers Ingagi shader declarations to Vulkan-compatible GLSL
// and compiles them to SPIR-V with glslang. It does not dispatch GPU work.
package shader

import (
	"context"
	"encoding/binary"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

var stageNames = map[string]string{
	"vertex": "vert", "fragment": "frag", "compute": "comp",
	"geometry": "geom", "tess_control": "tesc", "tess_evaluation": "tese",
}

// CompileSPIRV invokes glslangValidator with a bounded caller context. The
// optional executable defaults to PATH. Temporary source/artifacts are removed
// before return. Compiler diagnostics are included in returned errors.
func CompileSPIRV(ctx context.Context, s *fe.ShaderDecl, stage, executable string) ([]byte, error) {
	if ctx == nil {
		return nil, fmt.Errorf("ingagi: missing shader build context")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	glsl, err := CompileGLSL(s, stage)
	if err != nil {
		return nil, err
	}
	if executable == "" {
		executable = "glslangValidator"
	}
	dir, err := os.MkdirTemp("", "ingagi-shader-*")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	src, dst := filepath.Join(dir, "shader."+stageNames[stage]), filepath.Join(dir, "shader.spv")
	if err := os.WriteFile(src, []byte(glsl), 0600); err != nil {
		return nil, err
	}
	out, err := exec.CommandContext(ctx, executable, "-V", "--target-env", "vulkan1.1", "-S", stageNames[stage], "-o", dst, src).CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("ingagi: shader compiler: %w\n%s", err, out)
	}
	data, err := os.ReadFile(dst)
	if err != nil {
		return nil, err
	}
	if len(data) < 20 || len(data)%4 != 0 || binary.LittleEndian.Uint32(data) != 0x07230203 {
		return nil, fmt.Errorf("ingagi: shader compiler produced invalid SPIR-V")
	}
	return data, nil
}

// CompileGLSL emits a selected vertex, fragment or compute stage. Locals must
// have explicit GPU types. Unsupported host constructs/options are errors, never
// dropped. Resource bindings default to set zero and their declaration index.
func CompileGLSL(s *fe.ShaderDecl, stage string) (string, error) {
	if s == nil {
		return "", fmt.Errorf("ingagi: nil shader")
	}
	if stage != "vertex" && stage != "fragment" && stage != "compute" &&
		stage != "geometry" && stage != "tess_control" && stage != "tess_evaluation" {
		return "", fmt.Errorf("ingagi: shader stage %q is not lowered", stage)
	}
	if len(s.Options) > 0 || len(s.Attrs) > 0 {
		return "", fmt.Errorf("%s: shader declaration options/attributes are unsupported", s.Span)
	}
	reflection, err := Reflect(s, stage)
	if err != nil {
		return "", err
	}
	e := &glslEmitter{types: map[string]bool{}, names: map[string]bool{}}
	var selected *fe.ShaderStage
	for i := range s.Stages {
		st := &s.Stages[i]
		if st.Kind == stage {
			if selected != nil {
				e.fail(st.Span, "duplicate %s stage", stage)
			}
			selected = st
		}
	}
	if selected == nil {
		return "", fmt.Errorf("%s: shader %s has no %s stage", s.Span, s.Name, stage)
	}
	e.line("#version 450")
	for _, st := range s.Structs {
		if !st.Interface {
			e.types[st.Name] = true
		}
	}
	for _, st := range s.Structs {
		e.declare(st.Name, st.Span)
		if st.Interface {
			continue
		}
		e.line("struct %s {", e.ident(st.Name, st.Span))
		e.indent++
		for _, f := range st.Fields {
			if len(f.Attrs) > 0 {
				e.fail(f.Span, "field attributes are unsupported")
			}
			e.line("%s;", e.field(f.Type, f.Name, f.Span))
		}
		e.indent--
		e.line("};")
	}
	for _, c := range s.Consts {
		e.declare(c.Name, c.Span)
		e.line("const %s %s = %s;", e.typ(c.Type, c.Span), e.ident(c.Name, c.Span), e.expr(c.Value))
	}
	for i, c := range s.Specs {
		e.declare(c.Name, c.Span)
		if c.Value == nil {
			e.fail(c.Span, "specialization constant requires a default")
		}
		e.line("layout(constant_id = %d) const %s %s = %s;", i, e.typ(c.Type, c.Span), e.ident(c.Name, c.Span), e.expr(c.Value))
	}
	bindings := map[[2]int]bool{}
	for i, r := range s.Resources {
		set, binding := 0, i
		seen := map[string]bool{}
		for _, a := range r.Attrs {
			if seen[a.Name] || (a.Name != "set" && a.Name != "binding") || len(a.Args) != 1 {
				e.fail(a.Span, "unsupported or duplicate resource attribute %s", a.Name)
				continue
			}
			seen[a.Name] = true
			l, ok := a.Args[0].(*fe.Lit)
			if !ok || l.Kind != fe.LitInt || l.Int < 0 || l.Int > math.MaxInt32 {
				e.fail(a.Span, "binding/set must be a nonnegative int32 literal")
				continue
			}
			if a.Name == "set" {
				set = int(l.Int)
			} else {
				binding = int(l.Int)
			}
		}
		key := [2]int{set, binding}
		if bindings[key] {
			e.fail(r.Span, "duplicate resource set/binding %d/%d", set, binding)
		}
		bindings[key] = true
		e.declare(r.Name, r.Span)
		layout := fmt.Sprintf("layout(set = %d, binding = %d", set, binding)
		switch r.Kind {
		case "uniform", "storage":
			kind, access, packing := "uniform", "", "std140"
			if r.Kind == "storage" {
				kind, packing = "buffer", "std430"
				switch r.Access {
				case "read":
					access = "readonly "
				case "write":
					access = "writeonly "
				case "", "read_write":
				default:
					e.fail(r.Span, "unknown storage access")
				}
			}
			e.line("%s, %s) %s%s %s {", layout, packing, access, kind, e.ident(r.Name, r.Span))
			e.indent++
			for _, f := range r.Fields {
				if len(f.Attrs) > 0 {
					e.fail(f.Span, "buffer field attributes are unsupported")
				}
				e.line("%s;", e.field(f.Type, f.Name, f.Span))
			}
			e.indent--
			suffix := ""
			if r.Alias != "" {
				e.declare(r.Alias, r.Span)
				suffix = " " + e.ident(r.Alias, r.Span)
			}
			e.line("}%s;", suffix)
		case "texture1d", "texture2d", "texture2d_array", "texture3d", "texture_cube", "texture_cube_array", "texture2d_ms", "sampler":
			textures := map[string]string{"texture1d": "sampler1D", "texture2d": "sampler2D", "texture2d_array": "sampler2DArray", "texture3d": "sampler3D", "texture_cube": "samplerCube", "texture_cube_array": "samplerCubeArray", "texture2d_ms": "sampler2DMS", "sampler": "sampler"}
			e.line("%s) uniform %s %s;", layout, textures[r.Kind], e.ident(r.Name, r.Span))
		case "storage_image":
			images := map[string]string{"texture1d": "image1D", "texture2d": "image2D", "texture2d_array": "image2DArray", "texture3d": "image3D", "texture_cube": "imageCube", "texture_cube_array": "imageCubeArray", "texture2d_ms": "image2DMS"}
			typ, ok := images[r.Type]
			if !ok {
				e.fail(r.Span, "unsupported storage image type %s", r.Type)
				break
			}
			access := ""
			switch r.Access {
			case "read":
				access = "readonly "
			case "write":
				access = "writeonly "
			case "read_write", "":
			default:
				e.fail(r.Span, "unknown storage image access %s", r.Access)
			}
			// Ingagi's floating storage images have a fixed rgba32f texel format.
			// Vulkan requires a format qualifier for readable storage images.
			e.line("%s, rgba32f) uniform %s%s %s;", layout, access, typ, e.ident(r.Name, r.Span))
		default:
			e.fail(r.Span, "unsupported resource kind %s", r.Kind)
		}
	}
	for _, fn := range s.Funcs {
		e.declare(fn.Name, fn.Span)
		e.line("%s;", e.signature(fn))
	}
	for _, fn := range s.Funcs {
		e.line("%s", e.signature(fn))
		e.block(fn.Body)
	}
	if stage == "compute" {
		sizes := [3]int64{1, 1, 1}
		var options []string
		for name := range selected.Options {
			options = append(options, name)
		}
		sort.Strings(options)
		for _, name := range options {
			value := selected.Options[name]
			axis := -1
			switch name {
			case "local_size", "local_size_x":
				axis = 0
			case "local_size_y":
				axis = 1
			case "local_size_z":
				axis = 2
			}
			if axis < 0 {
				e.fail(selected.Span, "unsupported compute option %s", name)
				continue
			}
			l, ok := value.(*fe.Lit)
			if !ok || l.Kind != fe.LitInt || l.Int < 1 || l.Int > 1024 {
				e.fail(selected.Span, "local size must be a literal in 1..1024")
				continue
			}
			sizes[axis] = l.Int
		}
		if selected.Options["local_size"] != nil && selected.Options["local_size_x"] != nil {
			e.fail(selected.Span, "duplicate local size x options")
		}
		if sizes[0]*sizes[1]*sizes[2] > 1024 {
			e.fail(selected.Span, "workgroup contains more than 1024 invocations")
		}
		e.line("layout(local_size_x = %d, local_size_y = %d, local_size_z = %d) in;", sizes[0], sizes[1], sizes[2])
	} else if stage == "geometry" {
		e.emitGeometryLayout(selected)
	} else if stage == "tess_control" {
		e.emitTessControlLayout(selected)
	} else if stage == "tess_evaluation" {
		e.emitTessEvaluationLayout(selected)
	}
	e.emitInterfaces(reflection)
	e.ioActive = selected.Options["inputs"] != nil || selected.Options["outputs"] != nil
	e.line("void main()")
	e.block(selected.Body)
	if e.err != nil {
		return "", e.err
	}
	return e.out.String(), nil
}

func stageOptionToken(v fe.Expr) (string, bool) {
	switch v := v.(type) {
	case *fe.IdentExpr:
		return v.Name, true
	case *fe.PathExpr:
		return v.Path.String(), true
	case *fe.Lit:
		if v.Kind == fe.LitString {
			return v.Str, true
		}
	}
	return "", false
}

func stageOptionInt(v fe.Expr) (int64, bool) {
	l, ok := v.(*fe.Lit)
	if !ok || l.Kind != fe.LitInt {
		return 0, false
	}
	return l.Int, true
}

func (e *glslEmitter) optionToken(s *fe.ShaderStage, names ...string) (string, bool) {
	for _, name := range names {
		if v, ok := s.Options[name]; ok {
			token, valid := stageOptionToken(v)
			if !valid {
				e.fail(s.Span, "shader option %s must be an identifier or string", name)
			}
			return token, valid
		}
	}
	return "", false
}

func (e *glslEmitter) optionInt(s *fe.ShaderStage, names ...string) (int64, bool) {
	for _, name := range names {
		if v, ok := s.Options[name]; ok {
			n, valid := stageOptionInt(v)
			if !valid {
				e.fail(s.Span, "shader option %s must be an integer literal", name)
			}
			return n, valid
		}
	}
	return 0, false
}

func (e *glslEmitter) rejectUnknownOptions(s *fe.ShaderStage, allowed map[string]bool) {
	for name := range s.Options {
		if !allowed[name] {
			e.fail(s.Span, "unsupported %s option %s", s.Kind, name)
		}
	}
}

func (e *glslEmitter) emitGeometryLayout(s *fe.ShaderStage) {
	e.rejectUnknownOptions(s, map[string]bool{"input": true, "input_primitive": true, "output": true, "output_primitive": true, "max_vertices": true})
	in, ok := e.optionToken(s, "input_primitive", "input")
	if !ok {
		in = "triangles"
	}
	out, ok := e.optionToken(s, "output_primitive", "output")
	if !ok {
		out = "triangle_strip"
	}
	inputs := map[string]bool{"points": true, "lines": true, "lines_adjacency": true, "triangles": true, "triangles_adjacency": true}
	outputs := map[string]bool{"points": true, "line_strip": true, "triangle_strip": true}
	if !inputs[in] {
		e.fail(s.Span, "invalid geometry input primitive %s", in)
	}
	if !outputs[out] {
		e.fail(s.Span, "invalid geometry output primitive %s", out)
	}
	max, ok := e.optionInt(s, "max_vertices")
	if !ok {
		max = 1
	}
	if max < 1 || max > 1024 {
		e.fail(s.Span, "geometry max_vertices must be in 1..1024")
	}
	e.line("layout(%s) in;", in)
	e.line("layout(%s, max_vertices = %d) out;", out, max)
}

func (e *glslEmitter) emitTessControlLayout(s *fe.ShaderStage) {
	e.rejectUnknownOptions(s, map[string]bool{"vertices": true})
	vertices, ok := e.optionInt(s, "vertices")
	if !ok {
		vertices = 3
	}
	if vertices < 1 || vertices > 32 {
		e.fail(s.Span, "tessellation control vertices must be in 1..32")
	}
	e.line("layout(vertices = %d) out;", vertices)
}

func (e *glslEmitter) emitTessEvaluationLayout(s *fe.ShaderStage) {
	e.rejectUnknownOptions(s, map[string]bool{"primitive": true, "spacing": true, "ordering": true, "point_mode": true})
	primitive, ok := e.optionToken(s, "primitive")
	if !ok {
		primitive = "triangles"
	}
	spacing, hasSpacing := e.optionToken(s, "spacing")
	ordering, hasOrdering := e.optionToken(s, "ordering")
	point, hasPoint := s.Options["point_mode"]
	validPrimitive := map[string]bool{"triangles": true, "quads": true, "isolines": true}
	validSpacing := map[string]bool{"equal_spacing": true, "fractional_even_spacing": true, "fractional_odd_spacing": true}
	validOrdering := map[string]bool{"cw": true, "ccw": true}
	if !validPrimitive[primitive] {
		e.fail(s.Span, "invalid tessellation primitive %s", primitive)
	}
	if hasSpacing && !validSpacing[spacing] {
		e.fail(s.Span, "invalid tessellation spacing %s", spacing)
	}
	if hasOrdering && !validOrdering[ordering] {
		e.fail(s.Span, "invalid tessellation ordering %s", ordering)
	}
	parts := []string{}
	if hasSpacing {
		parts = append(parts, spacing)
	}
	if hasOrdering {
		parts = append(parts, ordering)
	}
	if hasPoint {
		l, ok := point.(*fe.Lit)
		if !ok || l.Kind != fe.LitBool {
			e.fail(s.Span, "tessellation point_mode must be a boolean literal")
		} else if l.Bool {
			parts = append(parts, "point_mode")
		}
	}
	parts = append(parts, primitive)
	e.line("layout(%s) in;", strings.Join(parts, ", "))
}

type glslEmitter struct {
	out             strings.Builder
	indent          int
	err             error
	types, names    map[string]bool
	ioActive        bool
	inputs, outputs map[string]InterfaceVariable
}

func (e *glslEmitter) emitInterfaces(r *StageReflection) {
	e.inputs = map[string]InterfaceVariable{}
	e.outputs = map[string]InterfaceVariable{}
	for _, group := range []struct {
		direction string
		variables []InterfaceVariable
		target    map[string]InterfaceVariable
	}{{"in", r.Inputs, e.inputs}, {"out", r.Outputs, e.outputs}} {
		for _, v := range group.variables {
			group.target[v.Name] = v
			qualifier := ""
			if r.Stage == "vertex" && group.direction == "out" || r.Stage == "fragment" && group.direction == "in" {
				qualifier = v.Interpolation + " "
				if v.Sampling != "" {
					qualifier = v.Sampling + " " + qualifier
				}
			}
			// Generated names never pass through ident(): the prefix is reserved to
			// prevent collisions with source globals, locals and parameters.
			name := "ig_interface_" + group.direction + "_" + v.Name
			e.line("layout(location = %d) %s%s %s;", v.Location, qualifier, group.direction, e.fieldGenerated(v.Type, name))
		}
	}
}
func (e *glslEmitter) fieldGenerated(t, name string) string {
	if elem, n, ok := arrayType(t); ok {
		return fmt.Sprintf("%s %s[%d]", e.typ(elem, fe.Span{}), name, n)
	}
	return e.typ(t, fe.Span{}) + " " + name
}

func (e *glslEmitter) fail(sp fe.Span, format string, args ...any) {
	if e.err == nil {
		e.err = fmt.Errorf("%s: shader: %s", sp, fmt.Sprintf(format, args...))
	}
}
func (e *glslEmitter) line(format string, args ...any) {
	e.out.WriteString(strings.Repeat("  ", e.indent))
	fmt.Fprintf(&e.out, format, args...)
	e.out.WriteByte('\n')
}
func (e *glslEmitter) ident(n string, sp fe.Span) string {
	if strings.HasPrefix(n, "ig_interface_") {
		e.fail(sp, "reserved generated identifier %s", n)
	}
	if n == "" {
		e.fail(sp, "empty identifier")
		return "invalid"
	}
	for i, c := range n {
		if !(c == '_' || c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || i > 0 && c >= '0' && c <= '9') {
			e.fail(sp, "invalid GPU identifier %q", n)
			return "invalid"
		}
	}
	return n
}
func (e *glslEmitter) declare(n string, sp fe.Span) {
	if n == "main" || strings.HasPrefix(n, "gl_") || e.names[n] {
		e.fail(sp, "duplicate or reserved GPU name %s", n)
	}
	e.names[n] = true
}
func (e *glslEmitter) typ(t string, sp fe.Span) string {
	switch t {
	case "f32":
		return "float"
	case "i32":
		return "int"
	case "u32":
		return "uint"
	case "", "void":
		return "void"
	case "bool", "float", "int", "uint", "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4", "bvec2", "bvec3", "bvec4", "mat2", "mat3", "mat4":
		return t
	}
	if e.types[t] {
		return e.ident(t, sp)
	}
	e.fail(sp, "unsupported GPU type %q", t)
	return "invalid"
}

// Fixed one-dimensional arrays in structs/buffers use Ingagi's existing
// [element; length] syntax. Bound literal sizes; host expressions and nested
// arrays require layout/constant evaluation that this emitter does not provide.
func (e *glslEmitter) field(t, name string, sp fe.Span) string {
	if strings.HasPrefix(t, "[") && strings.HasSuffix(t, "]") {
		parts := strings.Split(t[1:len(t)-1], ";")
		if len(parts) == 2 {
			size, err := strconv.Atoi(strings.TrimSpace(parts[1]))
			elem := strings.TrimSpace(parts[0])
			if err == nil && size > 0 && size <= 65536 && elem != "void" && elem != "" {
				return fmt.Sprintf("%s %s[%d]", e.typ(elem, sp), e.ident(name, sp), size)
			}
		}
		e.fail(sp, "GPU array requires an element type and literal length in 1..65536")
		return "invalid"
	}
	return e.typ(t, sp) + " " + e.ident(name, sp)
}
func (e *glslEmitter) signature(fn fe.ShaderFunc) string {
	var params []string
	for _, p := range fn.Params {
		if len(p.Attrs) > 0 {
			e.fail(p.Span, "parameter attributes are unsupported")
		}
		params = append(params, e.typ(p.Type, p.Span)+" "+e.ident(p.Name, p.Span))
	}
	return e.typ(fn.Ret, fn.Span) + " " + e.ident(fn.Name, fn.Span) + "(" + strings.Join(params, ", ") + ")"
}
func (e *glslEmitter) block(b *fe.Block) {
	if b == nil {
		e.fail(fe.Span{}, "missing shader block")
		return
	}
	e.line("{")
	e.indent++
	for _, st := range b.Stmts {
		e.stmt(st)
	}
	e.indent--
	e.line("}")
}
func (e *glslEmitter) stmt(s fe.Stmt) {
	switch s := s.(type) {
	case *fe.Block:
		e.block(s)
	case *fe.VarDeclStmt:
		if e.ioActive && (s.Name == "inputs" || s.Name == "outputs") {
			e.fail(s.Span, "stage interface namespaces cannot be shadowed")
		}
		t, ok := s.Type.(*fe.TypeName)
		if !ok || t.Optional || len(t.Args) > 0 || len(s.Attrs) > 0 {
			e.fail(s.Span, "shader local requires an explicit GPU type")
			return
		}
		init := ""
		if s.Init != nil {
			init = " = " + e.expr(s.Init)
		}
		e.line("%s %s%s;", e.typ(t.Path.String(), s.Span), e.ident(s.Name, s.Span), init)
	case *fe.ExprStmt:
		e.line("%s;", e.expr(s.X))
	case *fe.ReturnStmt:
		if s.Value == nil {
			e.line("return;")
		} else {
			e.line("return %s;", e.expr(s.Value))
		}
	case *fe.IfStmt:
		e.line("if (%s)", e.expr(s.Cond))
		e.block(s.Then)
		if s.ElseIf != nil {
			e.line("else")
			e.stmt(s.ElseIf)
		} else if s.Else != nil {
			e.line("else")
			e.block(s.Else)
		}
	case *fe.WhileStmt:
		e.line("while (%s)", e.expr(s.Cond))
		e.block(s.Body)
	case *fe.LoopStmt:
		e.line("while (true)")
		e.block(s.Body)
	case *fe.BreakStmt:
		if s.Label != "" || s.Value != nil {
			e.fail(s.Span, "valued/labeled break is unsupported")
		}
		e.line("break;")
	case *fe.ContinueStmt:
		if s.Label != "" {
			e.fail(s.Span, "labeled continue is unsupported")
		}
		e.line("continue;")
	default:
		e.fail(s.Pos(), "unsupported shader statement %T", s)
	}
}
func (e *glslEmitter) expr(x fe.Expr) string {
	if x == nil {
		e.fail(fe.Span{}, "missing expression")
		return "invalid"
	}
	switch x := x.(type) {
	case *fe.Lit:
		switch x.Kind {
		case fe.LitInt:
			if x.Int < math.MinInt32 || x.Int > math.MaxInt32 {
				e.fail(x.Span, "GPU integer literal exceeds int32")
			}
			return strconv.FormatInt(x.Int, 10)
		case fe.LitFloat:
			if math.IsNaN(x.Float) || math.IsInf(x.Float, 0) || math.Abs(x.Float) > math.MaxFloat32 {
				e.fail(x.Span, "invalid float32 literal")
			}
			n := strconv.FormatFloat(x.Float, 'g', -1, 32)
			if !strings.ContainsAny(n, ".eE") {
				n += ".0"
			}
			return n
		case fe.LitBool:
			return strconv.FormatBool(x.Bool)
		}
	case *fe.IdentExpr:
		if e.ioActive && (x.Name == "inputs" || x.Name == "outputs") {
			e.fail(x.Span, "interface namespace requires a field access")
		}
		return e.ident(x.Name, x.Span)
	case *fe.PathExpr:
		return e.ident(x.Path.String(), x.Span)
	case *fe.UnaryExpr:
		if x.Op != "!" && x.Op != "~" && x.Op != "-" && x.Op != "+" {
			e.fail(x.Span, "unsupported shader unary operator %s", x.Op)
		}
		return "(" + x.Op + e.expr(x.X) + ")"
	case *fe.BinaryExpr:
		switch x.Op {
		case "+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>", "==", "!=", "<", ">", "<=", ">=", "&&", "||":
		default:
			e.fail(x.Span, "unsupported shader binary operator %s", x.Op)
		}
		return "(" + e.expr(x.X) + " " + x.Op + " " + e.expr(x.Y) + ")"
	case *fe.AssignExpr:
		if e.ioActive {
			var root func(fe.Expr) string
			root = func(x fe.Expr) string {
				switch x := x.(type) {
				case *fe.IdentExpr:
					return x.Name
				case *fe.MemberExpr:
					return root(x.X)
				case *fe.IndexExpr:
					return root(x.X)
				}
				return ""
			}
			if root(x.X) == "inputs" {
				e.fail(x.Span, "stage inputs are read-only")
			}
		}
		return "(" + e.expr(x.X) + " " + x.Op + " " + e.expr(x.Y) + ")"
	case *fe.TernaryExpr:
		return "(" + e.expr(x.Cond) + " ? " + e.expr(x.Then) + " : " + e.expr(x.Else) + ")"
	case *fe.MemberExpr:
		if x.Arrow {
			e.fail(x.Span, "shader pointer members are unsupported")
		}
		if name, ok := x.X.(*fe.IdentExpr); ok && e.ioActive && (name.Name == "inputs" || name.Name == "outputs") {
			fields, direction := e.inputs, "in"
			if name.Name == "outputs" {
				fields, direction = e.outputs, "out"
			}
			if _, ok := fields[x.Field]; !ok {
				e.fail(x.Span, "unknown %s interface field %s", name.Name, x.Field)
			}
			return "ig_interface_" + direction + "_" + e.ident(x.Field, x.Span)
		}
		return e.expr(x.X) + "." + e.ident(x.Field, x.Span)
	case *fe.IndexExpr:
		return e.expr(x.X) + "[" + e.expr(x.Index) + "]"
	case *fe.CallExpr:
		var args []string
		for _, a := range x.Args {
			if a.Name != "" {
				e.fail(a.Span, "named shader arguments are unsupported")
			}
			args = append(args, e.expr(a.Value))
		}
		return e.expr(x.Fn) + "(" + strings.Join(args, ", ") + ")"
	}
	e.fail(x.Pos(), "unsupported shader expression %T", x)
	return "invalid"
}
