package compiler

import (
	"fmt"
	"strconv"

	"github.com/Chubek/JoltFX/ingagi/utils"
)

// ParseTargetSpec rejects unknown/duplicate fields and unsupported schema
// versions. Descriptors are declarative data, never shell commands.
func ParseTargetSpec(source string) (TargetSpec, error) {
	var s TargetSpec
	if len(source) > 65536 {
		return s, fmt.Errorf("target spec exceeds 64 KiB")
	}
	root, err := utils.ParseOne(source)
	if err != nil {
		return s, err
	}
	fields := root.Field("target")
	if fields == nil {
		return s, fmt.Errorf("expected (target ...)")
	}
	seen := map[string]bool{}
	for _, field := range fields {
		if !field.IsList() || len(field.List) < 2 || !field.List[0].IsAtom() {
			return s, fmt.Errorf("line %d: expected named field with values", field.Line)
		}
		key := field.List[0].Atom
		if seen[key] {
			return s, fmt.Errorf("duplicate target field %q", key)
		}
		seen[key] = true
		var values []string
		unique := map[string]bool{}
		for _, v := range field.List[1:] {
			if !v.IsAtom() {
				return s, fmt.Errorf("%s requires atom values", key)
			}
			text, err := utils.Unquote(v.Atom)
			if err != nil || text == "" || unique[text] {
				return s, fmt.Errorf("invalid/duplicate %s value", key)
			}
			unique[text] = true
			values = append(values, text)
		}
		switch key {
		case "artifacts":
			s.Artifacts = values
		case "platforms":
			s.Platforms = values
		case "requires":
			s.Requires = values
		case "features":
			s.Features = values
		case "schema", "id", "kind", "status", "driver", "description":
			if len(values) != 1 {
				return s, fmt.Errorf("%s requires one value", key)
			}
			switch key {
			case "schema":
				s.Schema, err = strconv.Atoi(values[0])
				if err != nil {
					return s, err
				}
			case "id":
				s.ID = values[0]
			case "kind":
				s.Kind = values[0]
			case "status":
				s.Status = values[0]
			case "driver":
				s.Driver = values[0]
			case "description":
				s.Description = values[0]
			}
		default:
			return s, fmt.Errorf("unknown target field %q", key)
		}
	}
	for _, key := range []string{"schema", "id", "kind", "status", "driver", "description", "artifacts", "platforms", "requires", "features"} {
		if !seen[key] {
			return s, fmt.Errorf("missing target field %q", key)
		}
	}
	if s.Schema != 1 {
		return s, fmt.Errorf("unsupported target schema %d", s.Schema)
	}
	if s.Status != "implemented" && s.Status != "planned" {
		return s, fmt.Errorf("invalid target status %q", s.Status)
	}
	if s.Kind != "vm" && s.Kind != "native" && s.Kind != "shader" && s.Kind != "adapter" {
		return s, fmt.Errorf("invalid target kind %q", s.Kind)
	}
	for _, r := range s.ID {
		if !(r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '-') {
			return s, fmt.Errorf("invalid target ID %q", s.ID)
		}
	}
	return s, nil
}
