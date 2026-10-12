package runtime

import "fmt"

// verifyDescriptors checks the AST-independent metadata used by both loaders.
// It does not require static call names to resolve: scripts may link host
// callbacks and dynamically supplied types at runtime.
func verifyDescriptors(m *Module) error {
	unique := func(what string, names []string) (map[string]bool, error) {
		seen := map[string]bool{}
		for _, name := range names {
			if name == "" {
				return nil, fmt.Errorf("ingagi: empty %s name", what)
			}
			if seen[name] {
				return nil, fmt.Errorf("ingagi: duplicate %s %q", what, name)
			}
			seen[name] = true
		}
		return seen, nil
	}
	for name := range m.Params {
		if _, ok := m.Tape.Entry[name]; !ok {
			return fmt.Errorf("ingagi: arity references missing entry %q", name)
		}
	}
	for name := range m.Tape.Entry {
		if name == "" {
			return fmt.Errorf("ingagi: empty entry name")
		}
	}
	for _, name := range sortedKeys(m.Types) {
		ti := m.Types[name]
		if name == "" {
			return fmt.Errorf("ingagi: empty type name")
		}
		switch ti.Kind {
		case "struct", "component", "event", "entity", "enum":
		default:
			return fmt.Errorf("ingagi: type %q has invalid kind %q", name, ti.Kind)
		}
		fields := make([]string, len(ti.Fields))
		for i, field := range ti.Fields {
			fields[i] = field.Name
		}
		known, err := unique("field of "+name, fields)
		if err != nil {
			return err
		}
		if _, err := unique("variant of "+name, ti.Variants); err != nil {
			return err
		}
		if ti.Kind != "enum" && len(ti.Variants) != 0 {
			return fmt.Errorf("ingagi: non-enum %q declares variants", name)
		}
		if ti.Kind == "enum" && len(ti.Fields) != 0 {
			return fmt.Errorf("ingagi: enum %q declares fields", name)
		}
		for field := range ti.Defaults {
			if !known[field] {
				return fmt.Errorf("ingagi: default for unknown field %s.%s", name, field)
			}
		}
		for method, entry := range ti.Methods {
			if _, ok := m.Tape.Entry[entry]; !ok || method == "" {
				return fmt.Errorf("ingagi: type %q method %q references missing entry %q", name, method, entry)
			}
		}
	}
	systems, err := unique("system", m.SystemNames)
	if err != nil {
		return err
	}
	for what, names := range map[string][]string{"shader": m.Shaders, "pipeline": m.Pipelines, "event": m.Events} {
		if _, err := unique(what, names); err != nil {
			return err
		}
	}
	for _, name := range m.Events {
		if ti := m.Types[name]; ti == nil || ti.Kind != "event" {
			return fmt.Errorf("ingagi: event %q has no event descriptor", name)
		}
	}
	seenHooks := map[[2]string]bool{}
	for _, h := range m.Hooks {
		if !systems[h.System] {
			return fmt.Errorf("ingagi: hook references missing system %q", h.System)
		}
		switch h.Kind {
		case "startup", "shutdown", "update", "fixed_update", "late_update", "pre_render", "post_render", "on_event":
		default:
			return fmt.Errorf("ingagi: invalid hook kind %q", h.Kind)
		}
		if _, ok := m.Tape.Entry[h.Entry]; !ok {
			return fmt.Errorf("ingagi: hook references missing entry %q", h.Entry)
		}
		key := [2]string{h.System, h.Kind}
		if seenHooks[key] {
			return fmt.Errorf("ingagi: duplicate hook %s::%s", h.System, h.Kind)
		}
		seenHooks[key] = true
		if h.Kind == "on_event" {
			// Existing scripts can publish ordinary structs as events.
			if ti := m.Types[h.Event]; ti == nil || ti.Kind == "enum" {
				return fmt.Errorf("ingagi: hook references missing event type %q", h.Event)
			}
			if m.Params[h.Entry] != 1 {
				return fmt.Errorf("ingagi: event hook entry %q must take one argument", h.Entry)
			}
		} else if h.Event != "" {
			return fmt.Errorf("ingagi: non-event hook %q declares event type", h.Entry)
		}
	}
	for name := range m.Entities {
		if _, ok := m.Tape.Entry["prefab::"+name]; !ok || name == "" {
			return fmt.Errorf("ingagi: prefab %q has no constructor entry", name)
		}
		if m.Params["prefab::"+name] != 1 {
			return fmt.Errorf("ingagi: prefab %q constructor must take one argument", name)
		}
	}
	return nil
}
