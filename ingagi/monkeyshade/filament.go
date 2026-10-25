package shader

import (
	"context"
	"encoding/binary"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"time"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// FilamentParameter maps an Ingagi resource access to MaterialInstance's named
// parameter API. Filament owns the binary layout; Vulkan bindings are not reused.
type FilamentParameter struct {
	Source string `json:"source"`
	Name   string `json:"name"`
	Type   string `json:"type"`
}
type FilamentReflection struct {
	Shader       string              `json:"shader"`
	ShadingModel string              `json:"shading_model"`
	Blending     string              `json:"blending"`
	VertexDomain string              `json:"vertex_domain"`
	DoubleSided  bool                `json:"double_sided"`
	Requires     []string            `json:"requires"`
	Parameters   []FilamentParameter `json:"parameters"`
	Varyings     []InterfaceVariable `json:"varyings"`
	Vertex       bool                `json:"vertex"`
}

// IsFilament reports an explicit material profile, using the existing shader
// declaration options grammar. Compilation still validates all options.
func IsFilament(s *fe.ShaderDecl) bool {
	if s != nil {
		for _, o := range s.Options {
			if o.Name == "target" {
				v, ok := stageOptionToken(o.Value)
				if ok && v == "filament" {
					return true
				}
			}
		}
	}
	return false
}

// CompileFilament emits a complete surface material, including its optional
// vertex stage. The body syntax and lowering are shared with CompileGLSL.
// Call prepareMaterial(material) in the fragment body after setting normals
// and before using lighting-dependent public Filament APIs.
func CompileFilament(s *fe.ShaderDecl) (string, error) {
	text, _, err := buildFilament(s)
	return text, err
}
func ReflectFilament(s *fe.ShaderDecl) (*FilamentReflection, error) {
	_, r, err := buildFilament(s)
	return r, err
}

type filamentEmitter struct {
	values map[string]string
	blocks map[string]map[string]string
	stage  string
}

func filamentReserved(name string) bool {
	return name == "material" || name == "materialVertex" || name == "prepareMaterial" || name == "materialParams" || name == "inputs" || name == "outputs" || strings.HasPrefix(name, "materialParams_") || strings.HasPrefix(name, "variable_") || strings.HasPrefix(name, "ig_filament_") || strings.HasPrefix(name, "gl_")
}
func (f *filamentEmitter) local(e *glslEmitter, name string, sp fe.Span) {
	if filamentReserved(name) || f.values[name] != "" || f.blocks[name] != nil {
		e.fail(sp, "Filament local/parameter shadows reserved name or resource %s", name)
	}
}
func (f *filamentEmitter) identifier(e *glslEmitter, name string, sp fe.Span) string {
	if v := f.values[name]; v != "" {
		return v
	}
	if f.blocks[name] != nil {
		e.fail(sp, "Filament uniform block %s requires a field access", name)
	}
	if name == "material" && f.stage == "" {
		e.fail(sp, "material is only available in a Filament stage body")
	}
	if name == "inputs" || name == "outputs" {
		e.fail(sp, "Filament interface namespace requires a stage field access")
	}
	if strings.HasPrefix(name, "gl_") || name == "materialParams" || strings.HasPrefix(name, "materialParams_") || strings.HasPrefix(name, "variable_") || strings.HasPrefix(name, "ig_filament_") {
		e.fail(sp, "reserved Filament identifier %s", name)
	}
	return e.ident(name, sp)
}
func shaderExprRoot(x fe.Expr) string {
	switch x := x.(type) {
	case *fe.IdentExpr:
		return x.Name
	case *fe.PathExpr:
		return x.Path.String()
	case *fe.MemberExpr:
		return shaderExprRoot(x.X)
	case *fe.IndexExpr:
		return shaderExprRoot(x.X)
	}
	return ""
}

func buildFilament(s *fe.ShaderDecl) (string, *FilamentReflection, error) {
	if s == nil {
		return "", nil, fmt.Errorf("shader: nil Filament declaration")
	}
	bad := func(format string, args ...any) (string, *FilamentReflection, error) {
		return "", nil, fmt.Errorf("%s: Filament: %s", s.Span, fmt.Sprintf(format, args...))
	}
	if !IsFilament(s) {
		return bad("requires shader(target: \"filament\")")
	}
	r := &FilamentReflection{Shader: s.Name, ShadingModel: "lit", Blending: "opaque", VertexDomain: "object", Requires: []string{}, Parameters: []FilamentParameter{}, Varyings: []InterfaceVariable{}}
	seen := map[string]bool{}
	for _, o := range s.Options {
		if seen[o.Name] {
			return bad("duplicate option %s", o.Name)
		}
		seen[o.Name] = true
		value, ok := stageOptionToken(o.Value)
		if o.Name == "double_sided" {
			v, valid := o.Value.(*fe.Lit)
			if !valid || v.Kind != fe.LitBool {
				return bad("double_sided requires a boolean")
			}
			r.DoubleSided = v.Bool
			continue
		}
		if !ok {
			return bad("%s requires an identifier or string", o.Name)
		}
		switch o.Name {
		case "target":
			if value != "filament" {
				return bad("invalid target")
			}
		case "shading_model":
			if value != "lit" && value != "unlit" {
				return bad("shading_model must be lit or unlit")
			}
			r.ShadingModel = value
		case "blending":
			if value != "opaque" && value != "transparent" && value != "masked" && value != "fade" && value != "add" {
				return bad("unsupported blending %s", value)
			}
			r.Blending = value
		case "vertex_domain":
			if value != "object" && value != "world" && value != "view" && value != "device" {
				return bad("unsupported vertex_domain %s", value)
			}
			r.VertexDomain = value
		case "requires":
			attrs := map[string]bool{}
			for _, name := range strings.Split(value, ",") {
				name = strings.TrimSpace(name)
				if name != "uv0" && name != "uv1" && name != "color" && !(len(name) == 7 && strings.HasPrefix(name, "custom") && name[6] >= '0' && name[6] <= '7') {
					return bad("unsupported required attribute %q", name)
				}
				if attrs[name] {
					return bad("duplicate required attribute %s", name)
				}
				attrs[name] = true
				r.Requires = append(r.Requires, name)
			}
		default:
			return bad("unsupported option %s", o.Name)
		}
	}
	if len(s.Specs) != 0 {
		return bad("specialization constants are unsupported; use const or uniform parameters")
	}
	// Reuse the existing layout, declaration, interface and stage-option gates on
	// an owned shallow descriptor. No AST nodes are modified during lowering.
	base := *s
	base.Options = nil
	stages := map[string]*fe.ShaderStage{}
	reflections := map[string]*StageReflection{}
	for i := range s.Stages {
		st := &s.Stages[i]
		if st.Kind != "vertex" && st.Kind != "fragment" {
			return bad("unsupported material stage %s", st.Kind)
		}
		if stages[st.Kind] != nil {
			return bad("duplicate %s stage", st.Kind)
		}
		stages[st.Kind] = st
		ref, err := Reflect(&base, st.Kind)
		if err != nil {
			return "", nil, err
		}
		reflections[st.Kind] = ref
		if st.Kind == "vertex" && len(ref.Inputs) != 0 || st.Kind == "fragment" && len(ref.Outputs) != 0 {
			return bad("use Filament material/public APIs for vertex attributes and fragment outputs")
		}
	}
	if stages["fragment"] == nil {
		return bad("a fragment stage is required")
	}
	if stages["vertex"] != nil {
		r.Vertex = true
		if _, err := LinkGraphics(&base, &base); err != nil {
			return "", nil, err
		}
		r.Varyings = reflections["vertex"].Outputs
	} else if len(reflections["fragment"].Inputs) != 0 {
		return bad("fragment inputs require a vertex stage")
	}
	for _, v := range r.Varyings {
		if v.Type != "vec4" || v.Location > 3 || v.Interpolation != "smooth" || v.Sampling != "" {
			return bad("Filament varyings require smooth vec4 at locations 0..3")
		}
	}
	f := &filamentEmitter{values: map[string]string{}, blocks: map[string]map[string]string{}}
	e := &glslEmitter{types: map[string]bool{}, names: map[string]bool{}, filament: f}
	global := func(name string, sp fe.Span) {
		e.declare(name, sp)
		e.ident(name, sp)
		if filamentReserved(name) {
			e.fail(sp, "reserved Filament declaration %s", name)
		}
	}
	for _, st := range s.Structs {
		global(st.Name, st.Span)
		if !st.Interface {
			e.types[st.Name] = true
		}
	}
	for _, c := range s.Consts {
		global(c.Name, c.Span)
	}
	for _, fn := range s.Funcs {
		global(fn.Name, fn.Span)
	}
	paramNames := map[string]bool{}
	add := func(source, name, typ string, sp fe.Span) {
		e.ident(name, sp)
		if paramNames[name] || len(r.Parameters) >= 128 {
			e.fail(sp, "duplicate Filament parameter or more than 128 parameters: %s", name)
		}
		paramNames[name] = true
		r.Parameters = append(r.Parameters, FilamentParameter{source, name, typ})
	}
	for _, res := range s.Resources {
		global(res.Name, res.Span)
		if len(res.Attrs) != 0 {
			return bad("explicit set/binding attributes are not Filament parameter bindings")
		}
		switch res.Kind {
		case "uniform":
			var fields map[string]string
			if res.Alias != "" {
				global(res.Alias, res.Span)
				fields = map[string]string{}
				f.blocks[res.Alias] = fields
			}
			for _, field := range res.Fields {
				if len(field.Attrs) != 0 {
					return bad("uniform field attributes are unsupported")
				}
				typ, err := filamentParameterType(field.Type)
				if err != nil {
					return "", nil, err
				}
				name := res.Name + "_" + field.Name
				source := field.Name
				if fields != nil {
					source = res.Alias + "." + field.Name
					fields[field.Name] = "materialParams." + name
				} else {
					global(field.Name, field.Span)
					f.values[field.Name] = "materialParams." + name
				}
				add(source, name, typ, field.Span)
			}
		case "texture2d", "texture2d_array", "texture_cube":
			typ := map[string]string{"texture2d": "sampler2d", "texture2d_array": "sampler2dArray", "texture_cube": "samplerCubemap"}[res.Kind]
			add(res.Name, res.Name, typ, res.Span)
			f.values[res.Name] = "materialParams_" + res.Name
		default:
			return bad("unsupported material resource %s", res.Kind)
		}
	}
	e.line("material {")
	e.indent++
	e.line("name : %s,", strconv.Quote(s.Name))
	e.line("shadingModel : %s,", r.ShadingModel)
	e.line("blending : %s,", r.Blending)
	e.line("vertexDomain : %s,", r.VertexDomain)
	e.line("doubleSided : %t,", r.DoubleSided)
	e.line("requires : [%s],", strings.Join(r.Requires, ", "))
	if len(r.Varyings) > 0 {
		max := 0
		for _, v := range r.Varyings {
			if v.Location > max {
				max = v.Location
			}
		}
		var names []string
		for i := 0; i <= max; i++ {
			names = append(names, fmt.Sprintf("ig_filament_v%d", i))
		}
		e.line("variables : [%s],", strings.Join(names, ", "))
	}
	e.line("parameters : [")
	e.indent++
	for i, p := range r.Parameters {
		comma := ""
		if i+1 < len(r.Parameters) {
			comma = ","
		}
		e.line("{ type : %s, name : %s }%s", p.Type, p.Name, comma)
	}
	e.indent--
	e.line("]")
	e.indent--
	e.line("}")
	for _, kind := range []string{"vertex", "fragment"} {
		st := stages[kind]
		if st == nil {
			continue
		}
		e.line("%s {", kind)
		e.indent++
		for _, decl := range s.Structs {
			if decl.Interface {
				continue
			}
			e.line("struct %s {", e.ident(decl.Name, decl.Span))
			e.indent++
			for _, field := range decl.Fields {
				if len(field.Attrs) != 0 {
					e.fail(field.Span, "field attributes are unsupported")
				}
				e.line("%s;", e.field(field.Type, field.Name, field.Span))
			}
			e.indent--
			e.line("};")
		}
		for _, c := range s.Consts {
			e.line("const %s %s = %s;", e.typ(c.Type, c.Span), e.ident(c.Name, c.Span), e.expr(c.Value))
		}
		for _, fn := range s.Funcs {
			e.line("%s;", e.signature(fn))
		}
		for _, fn := range s.Funcs {
			e.line("%s", e.signature(fn))
			e.block(fn.Body)
		}
		e.inputs = map[string]InterfaceVariable{}
		e.outputs = map[string]InterfaceVariable{}
		for _, v := range reflections[kind].Inputs {
			e.inputs[v.Name] = v
		}
		for _, v := range reflections[kind].Outputs {
			e.outputs[v.Name] = v
		}
		e.ioActive = true
		f.stage = kind
		if kind == "vertex" {
			e.line("void materialVertex(inout MaterialVertexInputs material)")
		} else {
			e.line("void material(inout MaterialInputs material)")
		}
		e.block(st.Body)
		e.ioActive = false
		f.stage = ""
		e.indent--
		e.line("}")
	}
	if e.err != nil {
		return "", nil, e.err
	}
	return e.out.String(), r, nil
}

func filamentParameterType(t string) (string, error) {
	base, n, array := arrayType(t)
	if array {
		t = base
	}
	types := map[string]string{"bool": "bool", "bvec2": "bool2", "bvec3": "bool3", "bvec4": "bool4", "float": "float", "vec2": "float2", "vec3": "float3", "vec4": "float4", "int": "int", "ivec2": "int2", "ivec3": "int3", "ivec4": "int4", "uint": "uint", "uvec2": "uint2", "uvec3": "uint3", "uvec4": "uint4", "mat3": "float3x3", "mat4": "float4x4"}
	typ, ok := types[canonicalGPUType(t)]
	if !ok {
		return "", fmt.Errorf("Filament: unsupported parameter type %q", t)
	}
	if array {
		typ += fmt.Sprintf("[%d]", n)
	}
	return typ, nil
}

// CompileFilamat compiles the entire material with matc, not glslang/SPIRV-Cross:
// Filament must generate its own entry points, layouts and backend variants.
// The package must be loaded with the matching Filament SDK version.
func CompileFilamat(ctx context.Context, s *fe.ShaderDecl, tools ToolOptions) ([]byte, error) {
	if ctx == nil {
		return nil, fmt.Errorf("ingagi: missing shader build context")
	}
	if err := ctx.Err(); err != nil {
		return nil, err
	}
	text, err := CompileFilament(s)
	if err != nil {
		return nil, err
	}
	api, platform := tools.FilamentAPI, tools.FilamentPlatform
	if api == "" {
		api = "opengl"
	}
	if platform == "" {
		platform = "all"
	}
	if api != "opengl" && api != "vulkan" && api != "metal" && api != "all" {
		return nil, fmt.Errorf("Filament: unsupported API %q", api)
	}
	if platform != "desktop" && platform != "mobile" && platform != "all" {
		return nil, fmt.Errorf("Filament: unsupported platform %q", platform)
	}
	matc := tools.Matc
	if matc == "" {
		matc = os.Getenv("INGAGI_MATC")
	}
	if matc == "" {
		matc = "matc"
	}
	dir, err := os.MkdirTemp("", "ingagi-filament-*")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(dir)
	src, dst := filepath.Join(dir, "material.mat"), filepath.Join(dir, "material.filamat")
	if err = os.WriteFile(src, []byte(text), 0600); err != nil {
		return nil, err
	}
	// Bound even callers that supply Background, and limit diagnostic buffering.
	ctx, cancel := context.WithTimeout(ctx, 30*time.Second)
	defer cancel()
	cmd := exec.CommandContext(ctx, matc, "--api", api, "--platform", platform, "--output", dst, src)
	cmd.WaitDelay = time.Second
	diagnostic := &filamentDiagnostics{}
	cmd.Stdout = diagnostic
	cmd.Stderr = diagnostic
	if err = cmd.Run(); err != nil {
		if ctx.Err() != nil {
			return nil, fmt.Errorf("ingagi: matc: %w", ctx.Err())
		}
		return nil, fmt.Errorf("ingagi: matc: %w\n%s", err, diagnostic.text.String())
	}
	info, err := os.Stat(dst)
	if err != nil {
		return nil, err
	}
	if !info.Mode().IsRegular() || info.Size() > 64*1024*1024 {
		return nil, fmt.Errorf("ingagi: invalid/oversized Filament package")
	}
	data, err := os.ReadFile(dst)
	if err != nil {
		return nil, err
	}
	if !validFilamat(data) {
		return nil, fmt.Errorf("ingagi: matc produced an invalid Filament package")
	}
	return data, nil
}

type filamentDiagnostics struct{ text strings.Builder }

func (d *filamentDiagnostics) Write(p []byte) (int, error) {
	n := len(p)
	remaining := 64*1024 - d.text.Len()
	if remaining > 0 {
		if len(p) > remaining {
			p = p[:remaining]
		}
		d.text.Write(p)
	}
	return n, nil
}

// The SDK package is an uncompressed sequence of uint64 tags + uint32 lengths.
// Check framing/version/shader chunks, leaving SDK-version compatibility to the host.
func validFilamat(data []byte) bool {
	version, shaders := false, false
	for len(data) > 0 {
		if len(data) < 12 {
			return false
		}
		tag := binary.LittleEndian.Uint64(data)
		n := uint64(binary.LittleEndian.Uint32(data[8:12]))
		data = data[12:]
		if n > uint64(len(data)) {
			return false
		}
		switch tag {
		case 0x4d41545f56455253: // MAT_VERS
			if version || n != 4 || binary.LittleEndian.Uint32(data) == 0 {
				return false
			}
			version = true
		case 0x4d41545f474c534c, 0x4d41545f53504952, 0x4d41545f4d45544c: // MAT_GLSL / MAT_SPIR / MAT_METL
			shaders = shaders || n > 0
		}
		data = data[int(n):]
	}
	return version && shaders
}
