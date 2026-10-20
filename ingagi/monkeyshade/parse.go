package shader

import (
	"fmt"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// Parse selects a shader from an import-free, shader-only compilation unit.
// An empty name requires exactly one declaration. Duplicate declarations and
// host declarations are errors rather than silently omitted source.
func Parse(source, name string) (*fe.ShaderDecl, error) {
	if len(source) > 1024*1024 {
		return nil, fmt.Errorf("monkeyshade: source exceeds 1 MiB")
	}
	p := fe.Parse(source)
	if !p.OK() {
		return nil, fmt.Errorf("monkeyshade: parse: %v", p.Errors)
	}
	if len(p.File.Imports) != 0 || len(p.File.Module.Parts) != 0 {
		return nil, fmt.Errorf("monkeyshade: standalone shader source cannot declare modules or imports")
	}
	var result *fe.ShaderDecl
	seen := map[string]bool{}
	for _, d := range p.File.Decls {
		s, ok := d.(*fe.ShaderDecl)
		if !ok {
			return nil, fmt.Errorf("monkeyshade: standalone source accepts shader declarations only")
		}
		if seen[s.Name] {
			return nil, fmt.Errorf("%s: duplicate shader %s", s.Span, s.Name)
		}
		seen[s.Name] = true
		if name == "" || name == s.Name {
			if result != nil {
				return nil, fmt.Errorf("monkeyshade: select a shader by name")
			}
			result = s
		}
	}
	if result == nil {
		return nil, fmt.Errorf("monkeyshade: shader %q not found", name)
	}
	return result, nil
}
