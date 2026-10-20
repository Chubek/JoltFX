package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"testing"
)

func TestShaderInterfaceLinkChecks(t *testing.T) {
	for _, test := range []struct {
		typ   string
		valid bool
	}{{"vec2", true}, {"vec3", false}} {
		source := `shader V { interface O {@location(0) uv:vec2;} vertex(outputs:O){outputs.uv=vec2(0.0);gl_Position=vec4(0.0);} }
  shader F {interface I {@location(0) uv:` + test.typ + `;} fragment(inputs:I){} }
  pipeline P {vertex:V::vertex;fragment:F::fragment;}`
		parsed := fe.Parse(source)
		if !parsed.OK() {
			t.Fatal(parsed.Errors)
		}
		diags := Check(parsed.File, DefaultOptions())
		hasError := false
		for _, d := range diags {
			if d.Sev == Error {
				hasError = true
			}
		}
		if hasError == test.valid {
			t.Fatalf("valid=%v: %v", test.valid, diags)
		}
	}
}
