package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"sort"
)

// CheckShader validates stage declarations and pipeline references. It does
// not claim GPU type verification; that belongs to the selected shader emitter.
func CheckShader(f *fe.File, _ *Registry) []Diagnostic {
	var out []Diagnostic
	shaders := map[string]map[string]bool{}
	err := func(msg string, sp fe.Span) {
		out = append(out, Diagnostic{Sev: Error, Code: "E0401", Msg: msg, Span: sp})
	}
	for _, d := range f.Decls {
		if s, ok := d.(*fe.ShaderDecl); ok {
			stages := map[string]bool{}
			shaders[s.Name] = stages
			for _, stage := range s.Stages {
				if stages[stage.Kind] {
					err("duplicate shader stage "+stage.Kind, stage.Span)
				}
				stages[stage.Kind] = true
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
		}
	}
	return out
}
