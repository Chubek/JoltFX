package sema

import (
	fe "github.com/Chubek/JoltFX/ingagi/frontend"
	"testing"
)

func TestECSAnalysis(t *testing.T) {
	f := &fe.File{Decls: []fe.Decl{
		&fe.ComponentDecl{Name: "Position"},
		&fe.SystemDecl{Name: "Move", Queries: []fe.SystemQuery{{With: []fe.QueryComp{{Type: pathTest("Position")}}, Without: []fe.QueryComp{{Type: pathTest("Position")}}}}, Deps: []fe.SystemDep{{Kind: "after", Target: "Draw"}}},
		&fe.SystemDecl{Name: "Draw", Deps: []fe.SystemDep{{Kind: "after", Target: "Move"}}},
	}}
	ds := CheckECS(f, BuildRegistry(f))
	if len(ds) != 2 || ds[0].Code != "E0302" || ds[1].Code != "E0304" {
		t.Fatal(ds)
	}
	f.Decls[1].(*fe.SystemDecl).Queries[0].Without = nil
	f.Decls[2].(*fe.SystemDecl).Deps = nil
	if ds := CheckECS(f, BuildRegistry(f)); len(ds) != 0 {
		t.Fatal(ds)
	}
}

func pathTest(parts ...string) fe.Path {
	p := fe.Path{}
	for _, n := range parts {
		p.Parts = append(p.Parts, fe.Ident{Name: n})
	}
	return p
}

func TestShaderAnalysis(t *testing.T) {
	f := &fe.File{Decls: []fe.Decl{
		&fe.ShaderDecl{Name: "Paint", Stages: []fe.ShaderStage{{Kind: "vertex"}, {Kind: "fragment"}}},
		&fe.PipelineDecl{Name: "P", Stages: map[string]fe.Path{"vertex": pathTest("Paint", "vertex"), "fragment": pathTest("Paint", "fragment")}},
	}}
	if ds := CheckShader(f, BuildRegistry(f)); len(ds) != 0 {
		t.Fatal(ds)
	}
	f.Decls[1].(*fe.PipelineDecl).Stages["fragment"] = pathTest("Paint", "missing")
	if ds := CheckShader(f, BuildRegistry(f)); len(ds) != 1 || ds[0].Code != "E0401" {
		t.Fatal(ds)
	}
}

func TestSyntaxAndImports(t *testing.T) {
	f := parseTest(t, `import lib as L; import lib as L; struct P { x: i64; x: i64; } fn f(a: i64, a: i64) { 1 = 2; }`)
	if ds := CheckImport(f, BuildRegistry(f)); len(ds) != 1 || ds[0].Code != "E0501" {
		t.Fatal(ds)
	}
	if ds := CheckSyntax(f, BuildRegistry(f)); len(ds) != 3 {
		t.Fatal(ds)
	}
}
