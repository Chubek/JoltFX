package compiler

import (
	"embed"
	"fmt"
	"sort"
)

// TargetSpec is schema v1 compiler capability metadata. Planned targets cannot
// be selected for artifact emission merely by installing a descriptor.
type TargetSpec struct {
	Schema                                   int
	ID, Kind, Status, Driver, Description    string
	Artifacts, Platforms, Requires, Features []string
}

//go:embed targets/*.sexp
var targetFiles embed.FS

func (s TargetSpec) RequireArtifact(artifact string) error {
	if s.Status != "implemented" {
		return fmt.Errorf("target %s is planned; no emitter is implemented", s.ID)
	}
	for _, a := range s.Artifacts {
		if a == artifact {
			return nil
		}
	}
	return fmt.Errorf("target %s does not emit %q", s.ID, artifact)
}

// Targets returns independently owned descriptors in ID order.
func Targets() ([]TargetSpec, error) {
	entries, err := targetFiles.ReadDir("targets")
	if err != nil {
		return nil, err
	}
	out := make([]TargetSpec, 0, len(entries))
	seen := map[string]bool{}
	for _, entry := range entries {
		data, err := targetFiles.ReadFile("targets/" + entry.Name())
		if err != nil {
			return nil, err
		}
		spec, err := ParseTargetSpec(string(data))
		if err != nil {
			return nil, fmt.Errorf("%s: %w", entry.Name(), err)
		}
		if entry.Name() != spec.ID+".sexp" || seen[spec.ID] {
			return nil, fmt.Errorf("target ID/file mismatch: %s", entry.Name())
		}
		seen[spec.ID] = true
		out = append(out, spec)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].ID < out[j].ID })
	return out, nil
}

func LookupTarget(id string) (TargetSpec, error) {
	specs, err := Targets()
	if err != nil {
		return TargetSpec{}, err
	}
	for _, spec := range specs {
		if spec.ID == id {
			return spec, nil
		}
	}
	return TargetSpec{}, fmt.Errorf("unknown Ingagi target %q", id)
}
