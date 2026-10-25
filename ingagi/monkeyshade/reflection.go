package shader

import (
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

const MaxInterfaceLocations = 64
const MaxBufferBytes = 4 * 1024 * 1024
const maxReflectionFields = 4096

// InterfaceVariable describes an entire location range. Arrays and matrices
// occupy consecutive locations; component packing is deliberately unsupported.
type InterfaceVariable struct {
	Name          string `json:"name"`
	Type          string `json:"type"`
	Location      int    `json:"location"`
	Locations     int    `json:"locations"`
	Interpolation string `json:"interpolation,omitempty"`
	Sampling      string `json:"sampling,omitempty"`
}

// Offsets are relative to the containing block/struct. Members on an array
// describe one element, not an expanded copy for every array index.
type FieldReflection struct {
	Name         string            `json:"name"`
	Type         string            `json:"type"`
	Offset       int               `json:"offset"`
	Size         int               `json:"size"`
	Alignment    int               `json:"alignment"`
	ArrayStride  int               `json:"array_stride,omitempty"`
	MatrixStride int               `json:"matrix_stride,omitempty"`
	Members      []FieldReflection `json:"members,omitempty"`
}
type ResourceReflection struct {
	Name       string            `json:"name"`
	Alias      string            `json:"alias,omitempty"`
	Kind       string            `json:"kind"`
	Descriptor string            `json:"descriptor"`
	Type       string            `json:"type,omitempty"`
	Set        int               `json:"set"`
	Binding    int               `json:"binding"`
	Access     string            `json:"access"`
	Layout     string            `json:"layout,omitempty"`
	Format     string            `json:"format,omitempty"`
	Size       int               `json:"size,omitempty"`
	Fields     []FieldReflection `json:"fields,omitempty"`
	Stages     []string          `json:"stages"`
}
type SpecializationReflection struct {
	Name string `json:"name"`
	Type string `json:"type"`
	ID   int    `json:"id"`
}
type StageReflection struct {
	Shader           string                     `json:"shader"`
	Stage            string                     `json:"stage"`
	Inputs           []InterfaceVariable        `json:"inputs"`
	Outputs          []InterfaceVariable        `json:"outputs"`
	Resources        []ResourceReflection       `json:"resources"`
	Specializations  []SpecializationReflection `json:"specializations"`
	LocalSize        [3]int                     `json:"local_size"`
	RequiredFeatures []string                   `json:"required_features"`
}
type CapabilityInfo struct {
	Stages                []string            `json:"stages"`
	Formats               []string            `json:"formats"`
	InterfaceStages       []string            `json:"interface_stages"`
	BufferLayouts         []string            `json:"buffer_layouts"`
	PipelineLinks         []string            `json:"pipeline_links"`
	ToolDependencies      map[string][]string `json:"tool_dependencies"`
	DeviceDispatch        bool                `json:"device_dispatch"`
	MeshTaskStages        bool                `json:"mesh_task_stages"`
	MaxInterfaceLocations int                 `json:"max_interface_locations"`
	MaxBufferBytes        int                 `json:"max_buffer_bytes"`
}

// Compiler support only: external cross-compilers and device features are not
// probed. Each artifact's stage restrictions still apply.
func Capabilities() CapabilityInfo {
	return CapabilityInfo{Stages: []string{"vertex", "fragment", "compute", "geometry", "tess_control", "tess_evaluation"}, Formats: []string{"glsl", "spirv", "hlsl", "glsl-opengl", "glsl-es", "msl", "wgsl", "reflect", "filament", "filamat"}, InterfaceStages: []string{"vertex", "fragment"}, BufferLayouts: []string{"std140", "std430"}, PipelineLinks: []string{"compute", "vertex-fragment"}, MaxInterfaceLocations: MaxInterfaceLocations, MaxBufferBytes: MaxBufferBytes,
		ToolDependencies: map[string][]string{"glsl": {}, "reflect": {}, "filament": {}, "filamat": {"matc"}, "spirv": {"glslangValidator"}, "hlsl": {"glslangValidator", "spirv-cross"}, "glsl-opengl": {"glslangValidator", "spirv-cross"}, "glsl-es": {"glslangValidator", "spirv-cross"}, "msl": {"glslangValidator", "spirv-cross"}, "wgsl": {"glslangValidator", "naga"}}}
}

func canonicalGPUType(t string) string {
	switch t {
	case "f32":
		return "float"
	case "i32":
		return "int"
	case "u32":
		return "uint"
	}
	return strings.TrimSpace(t)
}
func arrayType(t string) (string, int, bool) {
	t = strings.TrimSpace(t)
	if !strings.HasPrefix(t, "[") || !strings.HasSuffix(t, "]") {
		return "", 0, false
	}
	p := strings.Split(t[1:len(t)-1], ";")
	if len(p) != 2 {
		return "", 0, false
	}
	n, err := strconv.Atoi(strings.TrimSpace(p[1]))
	if err != nil || n < 1 || n > 65536 {
		return "", 0, false
	}
	elem := canonicalGPUType(strings.TrimSpace(p[0]))
	if strings.HasPrefix(elem, "[") {
		return "", 0, false
	}
	return elem, n, true
}
func interfaceType(t string) (string, int, bool, error) {
	if elem, n, ok := arrayType(t); ok {
		c, slots, integer, err := interfaceType(elem)
		if err != nil {
			return "", 0, false, err
		}
		if n > MaxInterfaceLocations/slots {
			return "", 0, false, fmt.Errorf("interface array exceeds location budget")
		}
		return fmt.Sprintf("[%s;%d]", c, n), slots * n, integer, nil
	}
	t = canonicalGPUType(t)
	switch t {
	case "float", "vec2", "vec3", "vec4":
		return t, 1, false, nil
	case "int", "uint", "ivec2", "ivec3", "ivec4", "uvec2", "uvec3", "uvec4":
		return t, 1, true, nil
	case "mat2":
		return t, 2, false, nil
	case "mat3":
		return t, 3, false, nil
	case "mat4":
		return t, 4, false, nil
	}
	return "", 0, false, fmt.Errorf("unsupported stage interface type %q", t)
}
func interfaceVariables(s *fe.ShaderDecl, st *fe.ShaderStage, direction string) ([]InterfaceVariable, error) {
	result := []InterfaceVariable{}
	option := st.Options[direction]
	if option == nil {
		return result, nil
	}
	if st.Kind != "vertex" && st.Kind != "fragment" {
		return nil, fmt.Errorf("%s: explicit interfaces are only supported in vertex/fragment stages", st.Span)
	}
	name, ok := option.(*fe.IdentExpr)
	if !ok {
		return nil, fmt.Errorf("%s: %s requires an interface name", option.Pos(), direction)
	}
	var decl *fe.ShaderStruct
	for i := range s.Structs {
		if s.Structs[i].Name == name.Name && s.Structs[i].Interface {
			decl = &s.Structs[i]
		}
	}
	if decl == nil {
		return nil, fmt.Errorf("%s: unknown shader interface %s", option.Pos(), name.Name)
	}
	if len(decl.Fields) > MaxInterfaceLocations {
		return nil, fmt.Errorf("%s: too many interface fields", decl.Span)
	}
	names := map[string]bool{}
	var occupied [MaxInterfaceLocations]bool
	varying := st.Kind == "vertex" && direction == "outputs" || st.Kind == "fragment" && direction == "inputs"
	for _, f := range decl.Fields {
		e := &glslEmitter{}
		e.ident(f.Name, f.Span)
		if e.err != nil {
			return nil, e.err
		}
		if f.Name == "main" || strings.HasPrefix(f.Name, "gl_") || names[f.Name] {
			return nil, fmt.Errorf("%s: duplicate/reserved interface field %s", f.Span, f.Name)
		}
		names[f.Name] = true
		typ, slots, integer, err := interfaceType(f.Type)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", f.Span, err)
		}
		baseType := typ
		if element, _, ok := arrayType(typ); ok {
			baseType = element
		}
		if st.Kind == "fragment" && direction == "outputs" && strings.HasPrefix(baseType, "mat") {
			return nil, fmt.Errorf("%s: fragment outputs cannot be matrix-valued", f.Span)
		}
		v := InterfaceVariable{Name: f.Name, Type: typ, Location: -1, Locations: slots}
		if varying {
			v.Interpolation = "smooth"
		}
		attrs := map[string]bool{}
		for _, a := range f.Attrs {
			if attrs[a.Name] {
				return nil, fmt.Errorf("%s: duplicate interface attribute %s", a.Span, a.Name)
			}
			attrs[a.Name] = true
			switch a.Name {
			case "location":
				if len(a.Args) != 1 {
					return nil, fmt.Errorf("%s: location needs one integer literal", a.Span)
				}
				n, ok := stageOptionInt(a.Args[0])
				if !ok || n < 0 || n >= MaxInterfaceLocations {
					return nil, fmt.Errorf("%s: location must be in 0..%d", a.Span, MaxInterfaceLocations-1)
				}
				v.Location = int(n)
			case "flat", "smooth", "noperspective":
				if len(a.Args) != 0 || !varying || attrs["flat"] && attrs["smooth"] || attrs["flat"] && attrs["noperspective"] || attrs["smooth"] && attrs["noperspective"] {
					return nil, fmt.Errorf("%s: invalid/conflicting interpolation qualifier", a.Span)
				}
				v.Interpolation = a.Name
			case "centroid", "sample":
				if len(a.Args) != 0 || !varying || v.Sampling != "" {
					return nil, fmt.Errorf("%s: invalid/conflicting sampling qualifier", a.Span)
				}
				v.Sampling = a.Name
			default:
				return nil, fmt.Errorf("%s: unsupported interface attribute %s", a.Span, a.Name)
			}
		}
		if v.Location < 0 || v.Location > MaxInterfaceLocations-slots {
			return nil, fmt.Errorf("%s: field needs a non-overflowing @location", f.Span)
		}
		if varying && integer && v.Interpolation != "flat" {
			return nil, fmt.Errorf("%s: integer varyings require @flat", f.Span)
		}
		if v.Interpolation == "flat" && v.Sampling != "" {
			return nil, fmt.Errorf("%s: flat varying cannot have sampling qualifiers", f.Span)
		}
		for i := v.Location; i < v.Location+slots; i++ {
			if occupied[i] {
				return nil, fmt.Errorf("%s: overlapping interface location %d", f.Span, i)
			}
			occupied[i] = true
		}
		result = append(result, v)
	}
	return result, nil
}

type gpuLayout struct {
	size, align, arrayStride, matrixStride, nodes int
	height                                        int
	members                                       []FieldReflection
}
type layoutBuilder struct {
	types    map[string]fe.ShaderStruct
	memo     map[string]gpuLayout
	visiting map[string]bool
}

func roundUp(n, a int) int { return (n + a - 1) / a * a }
func (b *layoutBuilder) typ(t, packing string, depth int) (gpuLayout, error) {
	if depth > 32 {
		return gpuLayout{}, fmt.Errorf("shader layout nesting exceeds 32")
	}
	t = canonicalGPUType(t)
	key := packing + "|" + t
	if v, ok := b.memo[key]; ok {
		if depth+v.height > 32 {
			return gpuLayout{}, fmt.Errorf("shader layout nesting exceeds 32")
		}
		return v, nil
	}
	if b.visiting[key] {
		return gpuLayout{}, fmt.Errorf("recursive shader struct %s", t)
	}
	b.visiting[key] = true
	defer delete(b.visiting, key)
	var v gpuLayout
	switch t {
	case "bool", "float", "int", "uint":
		v = gpuLayout{size: 4, align: 4, nodes: 1}
	case "vec2", "ivec2", "uvec2", "bvec2":
		v = gpuLayout{size: 8, align: 8, nodes: 1}
	case "vec3", "ivec3", "uvec3", "bvec3":
		v = gpuLayout{size: 12, align: 16, nodes: 1}
	case "vec4", "ivec4", "uvec4", "bvec4":
		v = gpuLayout{size: 16, align: 16, nodes: 1}
	case "mat2", "mat3", "mat4":
		n := int(t[3] - '0')
		align := 8
		if n > 2 {
			align = 16
		}
		if packing == "std140" {
			align = 16
		}
		v = gpuLayout{size: n * align, align: align, matrixStride: align, nodes: 1}
	default:
		if elem, n, ok := arrayType(t); ok {
			base, err := b.typ(elem, packing, depth+1)
			if err != nil {
				return v, err
			}
			align := base.align
			if packing == "std140" {
				align = roundUp(align, 16)
			}
			stride := roundUp(base.size, align)
			if n > MaxBufferBytes/stride {
				return v, fmt.Errorf("buffer array exceeds %d bytes", MaxBufferBytes)
			}
			v = gpuLayout{size: n * stride, align: align, arrayStride: stride, matrixStride: base.matrixStride, members: base.members, nodes: base.nodes}
			v.height = base.height + 1
		} else if st, ok := b.types[t]; ok && !st.Interface {
			var err error
			v, err = b.fields(st.Fields, packing, depth+1)
			if err != nil {
				return v, err
			}
			v.height++
		} else {
			return v, fmt.Errorf("unsupported buffer type %q", t)
		}
	}
	if depth+v.height > 32 {
		return gpuLayout{}, fmt.Errorf("shader layout nesting exceeds 32")
	}
	b.memo[key] = v
	return v, nil
}
func (b *layoutBuilder) fields(fields []fe.ShaderField, packing string, depth int) (gpuLayout, error) {
	v := gpuLayout{align: 1, nodes: 1}
	if len(fields) == 0 || len(fields) > maxReflectionFields {
		return v, fmt.Errorf("buffer/struct requires 1..%d fields", maxReflectionFields)
	}
	seen := map[string]bool{}
	for _, f := range fields {
		e := &glslEmitter{}
		e.ident(f.Name, f.Span)
		if e.err != nil {
			return v, e.err
		}
		if seen[f.Name] || strings.HasPrefix(f.Name, "gl_") || len(f.Attrs) != 0 {
			return v, fmt.Errorf("%s: duplicate/reserved field or unsupported buffer field attributes", f.Span)
		}
		seen[f.Name] = true
		child, err := b.typ(f.Type, packing, depth)
		if err != nil {
			return v, fmt.Errorf("%s: %w", f.Span, err)
		}
		offset := roundUp(v.size, child.align)
		if child.size > MaxBufferBytes-offset || v.nodes > maxReflectionFields-child.nodes {
			return v, fmt.Errorf("%s: reflected buffer exceeds byte/field budget", f.Span)
		}
		v.members = append(v.members, FieldReflection{f.Name, canonicalFieldType(f.Type), offset, child.size, child.align, child.arrayStride, child.matrixStride, child.members})
		v.size = offset + child.size
		if child.align > v.align {
			v.align = child.align
		}
		v.nodes += child.nodes
		if child.height > v.height {
			v.height = child.height
		}
	}
	if packing == "std140" {
		v.align = roundUp(v.align, 16)
	}
	v.size = roundUp(v.size, v.align)
	if v.size > MaxBufferBytes {
		return v, fmt.Errorf("buffer exceeds %d bytes", MaxBufferBytes)
	}
	return v, nil
}
func canonicalFieldType(t string) string {
	if elem, n, ok := arrayType(t); ok {
		return fmt.Sprintf("[%s;%d]", canonicalGPUType(elem), n)
	}
	return canonicalGPUType(t)
}

// Reflect describes declared source resources, including unused declarations.
// It is not post-optimization SPIR-V reflection or a device-capability probe.
func Reflect(s *fe.ShaderDecl, stage string) (*StageReflection, error) {
	if s == nil {
		return nil, fmt.Errorf("shader: nil declaration")
	}
	if _, ok := stageNames[stage]; !ok {
		return nil, fmt.Errorf("shader: stage %q is not lowered", stage)
	}
	if len(s.Attrs) != 0 || len(s.Options) != 0 {
		return nil, fmt.Errorf("%s: unsupported shader declaration options/attributes", s.Span)
	}
	var selected *fe.ShaderStage
	for i := range s.Stages {
		if s.Stages[i].Kind == stage {
			if selected != nil {
				return nil, fmt.Errorf("%s: duplicate shader stage", s.Stages[i].Span)
			}
			selected = &s.Stages[i]
		}
	}
	if selected == nil {
		return nil, fmt.Errorf("%s: missing %s stage", s.Span, stage)
	}
	r := &StageReflection{Shader: s.Name, Stage: stage, Resources: []ResourceReflection{}, Specializations: []SpecializationReflection{}, RequiredFeatures: []string{}}
	var err error
	r.Inputs, err = interfaceVariables(s, selected, "inputs")
	if err != nil {
		return nil, err
	}
	r.Outputs, err = interfaceVariables(s, selected, "outputs")
	if err != nil {
		return nil, err
	}
	e := &glslEmitter{types: map[string]bool{}, names: map[string]bool{}}
	types := map[string]fe.ShaderStruct{}
	for _, st := range s.Structs {
		e.declare(st.Name, st.Span)
		e.ident(st.Name, st.Span)
		types[st.Name] = st
		if !st.Interface {
			e.types[st.Name] = true
		}
	}
	for _, c := range s.Consts {
		e.declare(c.Name, c.Span)
		e.ident(c.Name, c.Span)
	}
	for i, c := range s.Specs {
		e.declare(c.Name, c.Span)
		typ := e.typ(c.Type, c.Span)
		if c.Value == nil {
			e.fail(c.Span, "specialization constant needs a default")
		}
		switch typ {
		case "bool", "float", "int", "uint":
		default:
			e.fail(c.Span, "specialization constants must be scalar")
		}
		r.Specializations = append(r.Specializations, SpecializationReflection{c.Name, typ, i})
	}
	for _, fn := range s.Funcs {
		e.declare(fn.Name, fn.Span)
		e.ident(fn.Name, fn.Span)
	}
	if len(s.Resources) > 128 {
		return nil, fmt.Errorf("shader: more than 128 resources")
	}
	builder := &layoutBuilder{types: types, memo: map[string]gpuLayout{}, visiting: map[string]bool{}}
	bindings := map[[2]int]bool{}
	features := map[string]bool{}
	for i, res := range s.Resources {
		rr := ResourceReflection{Name: res.Name, Alias: res.Alias, Kind: res.Kind, Set: 0, Binding: i, Access: res.Access, Stages: []string{stage}}
		e.declare(res.Name, res.Span)
		e.ident(res.Name, res.Span)
		attrs := map[string]bool{}
		for _, a := range res.Attrs {
			if attrs[a.Name] || (a.Name != "set" && a.Name != "binding") || len(a.Args) != 1 {
				return nil, fmt.Errorf("%s: unsupported/duplicate resource attribute %s", a.Span, a.Name)
			}
			attrs[a.Name] = true
			n, ok := stageOptionInt(a.Args[0])
			if !ok || n < 0 || n > math.MaxInt32 {
				return nil, fmt.Errorf("%s: binding/set must be a nonnegative int32 literal", a.Span)
			}
			if a.Name == "set" {
				rr.Set = int(n)
			} else {
				rr.Binding = int(n)
			}
		}
		key := [2]int{rr.Set, rr.Binding}
		if bindings[key] {
			return nil, fmt.Errorf("%s: duplicate resource set/binding", res.Span)
		}
		bindings[key] = true
		switch res.Kind {
		case "uniform", "storage":
			rr.Descriptor = "uniform_buffer"
			if res.Kind == "storage" {
				rr.Descriptor = "storage_buffer"
			}
			rr.Layout = "std140"
			if res.Kind == "storage" {
				rr.Layout = "std430"
				features["storage_buffers"] = true
			}
			if rr.Access == "" {
				rr.Access = "read"
				if res.Kind == "storage" {
					rr.Access = "read_write"
				}
			}
			if rr.Access != "read" && rr.Access != "write" && rr.Access != "read_write" || res.Kind == "uniform" && rr.Access != "read" {
				return nil, fmt.Errorf("%s: invalid buffer access", res.Span)
			}
			layout, err := builder.fields(res.Fields, rr.Layout, 0)
			if err != nil {
				return nil, err
			}
			rr.Size = layout.size
			rr.Fields = layout.members
			if res.Alias != "" {
				e.declare(res.Alias, res.Span)
				e.ident(res.Alias, res.Span)
			} else {
				for _, f := range res.Fields {
					e.declare(f.Name, f.Span)
				}
			}
		case "texture1d", "texture2d", "texture2d_array", "texture3d", "texture_cube", "texture_cube_array", "texture2d_ms":
			rr.Descriptor = "combined_image_sampler"
			if res.Type != "" && canonicalGPUType(res.Type) != "float" {
				return nil, fmt.Errorf("%s: only floating sampled textures are lowered", res.Span)
			}
			rr.Type = res.Kind
			rr.Access = "read"
			features["sampled_textures"] = true
		case "sampler":
			rr.Descriptor = "sampler"
			rr.Access = "read"
		case "storage_image":
			rr.Descriptor = "storage_image"
			if !textureKindsSupported(res.Type) {
				return nil, fmt.Errorf("%s: unsupported storage image dimension", res.Span)
			}
			rr.Type = res.Type
			rr.Format = "rgba32f"
			if rr.Access == "" {
				rr.Access = "read_write"
			}
			if rr.Access != "read" && rr.Access != "write" && rr.Access != "read_write" {
				return nil, fmt.Errorf("%s: invalid image access", res.Span)
			}
			features["storage_images"] = true
		default:
			return nil, fmt.Errorf("%s: unsupported resource kind %s", res.Span, res.Kind)
		}
		r.Resources = append(r.Resources, rr)
	}
	for _, vars := range [][]InterfaceVariable{r.Inputs, r.Outputs} {
		for _, v := range vars {
			if v.Sampling == "sample" {
				features["sample_rate_shading"] = true
			}
		}
	}
	if selected.Options["inputs"] != nil || selected.Options["outputs"] != nil {
		if e.names["inputs"] || e.names["outputs"] {
			return nil, fmt.Errorf("%s: global declarations cannot shadow stage interface namespaces", selected.Span)
		}
	}
	if stage == "vertex" || stage == "fragment" {
		e.rejectUnknownOptions(selected, map[string]bool{"inputs": true, "outputs": true})
	} else if stage == "compute" {
		r.LocalSize = [3]int{1, 1, 1}
		e.rejectUnknownOptions(selected, map[string]bool{"local_size": true, "local_size_x": true, "local_size_y": true, "local_size_z": true})
		if selected.Options["local_size"] != nil && selected.Options["local_size_x"] != nil {
			e.fail(selected.Span, "duplicate local size x options")
		}
		for name, value := range selected.Options {
			axis := 0
			if name == "local_size_y" {
				axis = 1
			} else if name == "local_size_z" {
				axis = 2
			}
			n, ok := stageOptionInt(value)
			if !ok || n < 1 || n > 1024 {
				e.fail(selected.Span, "local size must be a literal in 1..1024")
			} else {
				r.LocalSize[axis] = int(n)
			}
		}
		if r.LocalSize[0]*r.LocalSize[1]*r.LocalSize[2] > 1024 {
			e.fail(selected.Span, "workgroup exceeds 1024 invocations")
		}
	} else if stage == "geometry" {
		e.emitGeometryLayout(selected)
		features["geometry_shader"] = true
	} else if stage == "tess_control" {
		e.emitTessControlLayout(selected)
		features["tessellation_shader"] = true
	} else if stage == "tess_evaluation" {
		e.emitTessEvaluationLayout(selected)
		features["tessellation_shader"] = true
	}
	if e.err != nil {
		return nil, e.err
	}
	for feature := range features {
		r.RequiredFeatures = append(r.RequiredFeatures, feature)
	}
	sort.Strings(r.RequiredFeatures)
	return r, nil
}
func textureKindsSupported(kind string) bool {
	switch kind {
	case "texture1d", "texture2d", "texture2d_array", "texture3d", "texture_cube", "texture_cube_array", "texture2d_ms":
		return true
	}
	return false
}
