package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"testing"
)

func TestFilamentProfile(t *testing.T) {
	for _, tc := range []struct {
		source string
		valid  bool
	}{
		{`shader M(target:"filament"){fragment{prepareMaterial(material);material.baseColor=vec4(1.0);}}`, true},
		{`shader M(target:"filament"){compute{}}`, false},
		{`shader M(target:"filament",bad:true){fragment{}}`, false},
		{`shader M(target:"filament"){vertex{}fragment{}} pipeline P{vertex:M::vertex;fragment:M::fragment;}`, false},
	} {
		p := fe.Parse(tc.source)
		if !p.OK() {
			t.Fatal(p.Errors)
		}
		d := CheckShader(p.File, nil)
		if (len(d) == 0) != tc.valid {
			t.Fatalf("%s: %v", tc.source, d)
		}
	}
}
