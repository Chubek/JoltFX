package noise

import (
	"encoding/json"
	"fmt"
	"io"
	"strings"
)

// ParseProfile accepts a bounded JSON object, applying DefaultProfile(0) for
// omitted fields. Unknown/duplicate fields, trailing values and null are errors.
// This uses JSON rather than introducing a second, incompatible Ingagi grammar.
func ParseProfile(source string) (Profile, error) {
	p := DefaultProfile(0)
	if len(source) > 4096 {
		return Profile{}, fmt.Errorf("noise: profile exceeds 4096 bytes")
	}
	d := json.NewDecoder(strings.NewReader(source))
	open, err := d.Token()
	if err != nil || open != json.Delim('{') {
		return Profile{}, fmt.Errorf("noise: expected profile object")
	}
	seen := map[string]bool{}
	for d.More() {
		key, err := d.Token()
		if err != nil {
			return Profile{}, err
		}
		name, ok := key.(string)
		if !ok || seen[name] {
			return Profile{}, fmt.Errorf("noise: invalid or duplicate profile field %v", key)
		}
		seen[name] = true
		var dst any
		switch name {
		case "seed":
			dst = &p.Seed
		case "octaves":
			dst = &p.Octaves
		case "frequency":
			dst = &p.Frequency
		case "lacunarity":
			dst = &p.Lacunarity
		case "gain":
			dst = &p.Gain
		default:
			return Profile{}, fmt.Errorf("noise: unknown profile field %q", name)
		}
		// json.Unmarshal treats null as a no-op on scalars; explicitly reject it.
		var raw json.RawMessage
		if err := d.Decode(&raw); err != nil {
			return Profile{}, err
		}
		if string(raw) == "null" {
			return Profile{}, fmt.Errorf("noise: null profile field %s", name)
		}
		if err := json.Unmarshal(raw, dst); err != nil {
			return Profile{}, err
		}
	}
	if _, err := d.Token(); err != nil {
		return Profile{}, err
	}
	if _, err := d.Token(); err != io.EOF {
		return Profile{}, fmt.Errorf("noise: trailing profile input")
	}
	if err := p.Validate(); err != nil {
		return Profile{}, err
	}
	return p, nil
}
