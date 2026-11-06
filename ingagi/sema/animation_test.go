package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"testing"
)

func TestAnimationTypesAndShadows(t *testing.T) {
	for _, src := range []string{`fn f(){return anim_ease(3,0.5);}`, `fn f(){return anim_curve([vec3(0,0,0),vec3(1,1,1)],3);}`, `fn f(){return anim_rect(vec2(0,0),vec2(1,1),0,0);}`} {
		p := fe.Parse(src)
		if !p.OK() {
			t.Fatal(p.Errors)
		}
		found := false
		for _, d := range Check(p.File, DefaultOptions()) {
			if d.Code == "E0201" {
				found = true
			}
		}
		if !found {
			t.Fatal("missed invalid animation signature", src)
		}
	}
	p := fe.Parse(`fn anim_ease(x: i64)->i64{return x;} fn f(){return anim_ease(2);}`)
	for _, d := range Check(p.File, DefaultOptions()) {
		if d.Sev == Error {
			t.Fatal(d)
		}
	}
	p = fe.Parse(`fn f(){return anim_track([0,1],[0,10],0.5,"linear");}`)
	for _, d := range Check(p.File, DefaultOptions()) {
		if d.Sev == Error {
			t.Fatal(d)
		}
	}
}
