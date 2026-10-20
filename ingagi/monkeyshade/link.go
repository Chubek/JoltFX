package shader

import (
	"fmt"
	"sort"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// PipelineReflection is a checked source-level contract, not an executable GPU
// pipeline. Resource bindings are merged and ordered by descriptor set/binding.
type PipelineReflection struct {
	Name                        string               `json:"name,omitempty"`
	Stages                      []StageReflection    `json:"stages"`
	Resources                   []ResourceReflection `json:"resources"`
	FixedFunctionStateValidated bool                 `json:"fixed_function_state_validated"`
}

func sameFields(a, b []FieldReflection) bool {
	if len(a) != len(b) {
		return false
	}
	for i, x := range a {
		y := b[i]
		if x.Offset != y.Offset || x.Size != y.Size || x.Alignment != y.Alignment || x.ArrayStride != y.ArrayStride || x.MatrixStride != y.MatrixStride || !sameFields(x.Members, y.Members) {
			return false
		}
		if len(x.Members) == 0 && x.Type != y.Type {
			return false
		}
	}
	return true
}
func sameResource(a, b ResourceReflection) bool {
	return a.Kind == b.Kind && a.Descriptor == b.Descriptor && a.Type == b.Type && a.Layout == b.Layout && a.Format == b.Format && a.Size == b.Size && sameFields(a.Fields, b.Fields)
}

// LinkGraphics matches fragment inputs to vertex outputs by location range,
// canonical type, interpolation and sampling. Names are deliberately irrelevant.
// Unused vertex outputs are allowed. Shared resource bindings must agree in
// descriptor kind and byte layout; stage-specific read/write access is merged.
func LinkGraphics(vertex, fragment *fe.ShaderDecl) (*PipelineReflection, error) {
	v, err := Reflect(vertex, "vertex")
	if err != nil {
		return nil, err
	}
	f, err := Reflect(fragment, "fragment")
	if err != nil {
		return nil, err
	}
	outputs := map[int]InterfaceVariable{}
	for _, o := range v.Outputs {
		outputs[o.Location] = o
	}
	for _, input := range f.Inputs {
		o, ok := outputs[input.Location]
		if !ok || o.Type != input.Type || o.Locations != input.Locations || o.Interpolation != input.Interpolation || o.Sampling != input.Sampling {
			return nil, fmt.Errorf("%s: fragment input %s at location %d does not match a vertex output", fragment.Span, input.Name, input.Location)
		}
	}
	result := &PipelineReflection{Stages: []StageReflection{*v, *f}, Resources: []ResourceReflection{}}
	bindings := map[[2]int]int{}
	for _, stage := range result.Stages {
		for _, r := range stage.Resources {
			key := [2]int{r.Set, r.Binding}
			if index, ok := bindings[key]; ok {
				old := &result.Resources[index]
				if !sameResource(*old, r) {
					return nil, fmt.Errorf("shader link: incompatible resource at set %d binding %d", r.Set, r.Binding)
				}
				old.Stages = append(old.Stages, stage.Stage)
				if old.Access != r.Access {
					old.Access = "read_write"
				}
			} else {
				bindings[key] = len(result.Resources)
				result.Resources = append(result.Resources, r)
			}
		}
	}
	sort.Slice(result.Resources, func(i, j int) bool {
		a, b := result.Resources[i], result.Resources[j]
		return a.Set < b.Set || a.Set == b.Set && a.Binding < b.Binding
	})
	return result, nil
}

// ReflectPipeline supports compute-only and direct vertex/fragment pipelines.
// Geometry/tessellation interface linking and fixed-function state validation
// remain separate work. Unsupported stage topologies fail; fixed-function state
// is explicitly marked unvalidated, not silently treated as an executable state.
func ReflectPipeline(file *fe.File, p *fe.PipelineDecl) (*PipelineReflection, error) {
	if file == nil || p == nil {
		return nil, fmt.Errorf("shader: pipeline/file is required")
	}
	shaders := map[string]*fe.ShaderDecl{}
	for _, d := range file.Decls {
		if s, ok := d.(*fe.ShaderDecl); ok {
			if shaders[s.Name] != nil {
				return nil, fmt.Errorf("shader: duplicate declaration %s", s.Name)
			}
			shaders[s.Name] = s
		}
	}
	resolve := func(kind string) (*fe.ShaderDecl, error) {
		ref, ok := p.Stages[kind]
		if !ok || len(ref.Parts) < 2 {
			return nil, fmt.Errorf("%s: pipeline requires %s: Shader::%s", p.Span, kind, kind)
		}
		name := fe.Path{Parts: ref.Parts[:len(ref.Parts)-1]}.String()
		s := shaders[name]
		if s == nil || ref.Last() != kind {
			return nil, fmt.Errorf("%s: unknown/mismatched pipeline stage %s", ref.Span, ref.String())
		}
		return s, nil
	}
	if len(p.Stages) == 1 && p.Stages["compute"].Last() == "compute" {
		s, err := resolve("compute")
		if err != nil {
			return nil, err
		}
		r, err := Reflect(s, "compute")
		if err != nil {
			return nil, err
		}
		return &PipelineReflection{Name: p.Name, Stages: []StageReflection{*r}, Resources: r.Resources}, nil
	}
	if len(p.Stages) != 2 {
		return nil, fmt.Errorf("%s: source linking supports compute-only or direct vertex/fragment pipelines", p.Span)
	}
	v, err := resolve("vertex")
	if err != nil {
		return nil, err
	}
	f, err := resolve("fragment")
	if err != nil {
		return nil, err
	}
	r, err := LinkGraphics(v, f)
	if err != nil {
		return nil, err
	}
	r.Name = p.Name
	return r, nil
}
