package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	shader "github.com/Chubek/JoltFX/ingagi/monkeyshade"
	"sort"
)

// CheckShader validates stage declarations, source layouts and direct graphics
// interface links. Full GPU expression/type verification still needs glslang.
func CheckShader(f *fe.File, _ *Registry) []Diagnostic {
	var out []Diagnostic
	shaders := map[string]map[string]bool{}
	declarations := map[string]*fe.ShaderDecl{}
	lowered := map[string]bool{}
	for _, stage := range shader.Capabilities().Stages {
		lowered[stage] = true
	}
	err := func(msg string, sp fe.Span) {
		out = append(out, Diagnostic{Sev: Error, Code: "E0401", Msg: msg, Span: sp})
	}
	for _, d := range f.Decls {
		if s, ok := d.(*fe.ShaderDecl); ok {
			declarations[s.Name] = s
			stages := map[string]bool{}
			shaders[s.Name] = stages
			for _, stage := range s.Stages {
				if stages[stage.Kind] {
					err("duplicate shader stage "+stage.Kind, stage.Span)
				}
				stages[stage.Kind] = true
				if lowered[stage.Kind] {
					if _, validation := shader.Reflect(s, stage.Kind); validation != nil {
						out = append(out, Diagnostic{Sev: Error, Code: "E0402", Msg: validation.Error(), Span: stage.Span})
					}
				}
				if stage.Kind == "compute" {
					for _, axis := range []string{"local_size", "local_size_x", "local_size_y", "local_size_z"} {
						if e := stage.Options[axis]; e != nil {
							n, eerr := constantInt(e)
							if eerr != nil || n == nil || !n.IsInt64() || n.Int64() < 1 || n.Int64() > 1024 {
								err("compute "+axis+" must be a constant in 1..1024", stage.Span)
							}
						}
					}
				}
			}
			seen := map[string]bool{}
			for _, res := range s.Resources {
				if seen[res.Name] {
					err("duplicate shader resource "+res.Name, res.Span)
				}
				seen[res.Name] = true
			}
		}
	}
	for _, d := range f.Decls {
		if p, ok := d.(*fe.PipelineDecl); ok {
			if _, compute := p.Stages["compute"]; compute && len(p.Stages) > 1 {
				err("compute pipeline cannot include graphics stages", p.Span)
			}
			var kinds []string
			for k := range p.Stages {
				kinds = append(kinds, k)
			}
			sort.Strings(kinds)
			for _, kind := range kinds {
				ref := p.Stages[kind]
				parts := ref.Parts
				if len(parts) < 2 {
					err("pipeline stage requires Shader::stage", ref.Span)
					continue
				}
				shader := fe.Path{Parts: parts[:len(parts)-1]}.String()
				stage := parts[len(parts)-1].Name
				if !shaders[shader][stage] {
					err("unknown shader stage "+ref.String(), ref.Span)
				} else if stage != kind {
					err("pipeline stage "+kind+" refers to "+stage, ref.Span)
				}
			}
			// Link the supported direct graphics topology only. Geometry/tessellation
			// retain reference checks but are not advertised as interface-linked.
			if len(p.Stages) == 2 && p.Stages["vertex"].Last() == "vertex" && p.Stages["fragment"].Last() == "fragment" {
				find := func(kind string) *fe.ShaderDecl {
					ref := p.Stages[kind]
					if len(ref.Parts) < 2 {
						return nil
					}
					return declarations[fe.Path{Parts: ref.Parts[:len(ref.Parts)-1]}.String()]
				}
				if v, f := find("vertex"), find("fragment"); v != nil && f != nil {
					if _, validation := shader.LinkGraphics(v, f); validation != nil {
						out = append(out, Diagnostic{Sev: Error, Code: "E0403", Msg: validation.Error(), Span: p.Span})
					}
				}
			}
		}
	}
	return out
}
