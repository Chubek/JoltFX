package frontend

import (
	"strings"
	"testing"
)

const sample = `
module game::demo;

import std::math as math;
import std::ecs;

@hot @replicate
component Position {
    x: f32 = 0.0;
    y: f32 = 0.0;
}

component Velocity {
    vx: f32;
    vy: f32;
}

struct Stats {
    hp: i32;
    name: string;
    fn heal(amount: i32) -> i32;
}

enum Dir : i32 {
    North = 0, South, East, West
}

const Gravity: f32 = -9.81;
type Vec = math::Vec2;

entity Player {
    Position { x: 1.0, y: 2.0 };
    Velocity { vx: 0.0, vy: 0.0 };
    child gun: Muzzle { offset: 1.0 };
    score: i32 = 0;
}

event Damage {
    amount: i32;
    from: string;
}

resource TimeScale: f32 = 1.0;

system Movement {
    query mover with (&mut Position as pos, &Velocity) without (Frozen) optional (&Health);
    before Physics;
    after Input;
    parallel true;
    fn damp(factor: f32) -> f32 {
        return factor * 0.99;
    }
    startup() {
        let t: f32 = 0.0;
    }
    update(dt: f32) {
        for each (&mut Position as p, &Velocity as v) in world {
            p.x = p.x + v.vx * dt;
        }
        let x = if dt > 0.0 { 1 } else { 2 };
        match x { 1 => { emit Damage{ amount: 10 }; }, _ => {} }
    }
    fixed_update(tick: u64) {
        loop { break; }
        while false { continue; }
        for p in items { p; }
    }
    on_event(e: Damage) {
        defer { finish(); }
        unsafe { raw(); }
    }
}

@client
system ClientFx {
    query fx with (Position);
    pre_render() {}
    post_render() {}
    late_update(dt: f32) {}
    shutdown() {}
}

shader Lit(color: vec4) {
    struct VertIn { @location(0) pos: vec3; @location(1) uv: vec2; }
    const MaxLights: i32 = 8;
    specialize Exposure: f32 = 1.0;
    @group(0) @binding(0) uniform Camera { viewProj: mat4; } cam;
    @group(1) texture2d albedo;
    sampler texSamp;
    storage_image texture2d outImg: write;
    read storage buffer Stats { count: u32; } stats;
    fn shade(n: vec3) -> vec3 {
        return n * 0.5 + 0.5;
    }
    vertex { pos_out = viewProj * vec4(pos, 1.0); }
    fragment { color = texture(albedo, uv) * Exposure; }
    compute(local_size: 64) { step(); }
}

pipeline SpritePipe {
    vertex: Lit::vertex;
    fragment: Lit::fragment;
    layout: StdLayout;
    vertex_layout: SpriteVerts;
    raster { cull: "back"; fill: "solid"; }
    depth { test: true; write: true; }
    blend { enabled: true; }
    target { format: "rgba8"; }
}

interface Drawable {
    fn draw(dt: f32);
}

extend Vec2 {
    fn len() -> f32 { return 1.0; }
}

extern "host" {
    fn host_log(msg: string);
    log_level: i32;
}
`

func TestParseSample(t *testing.T) {
	r := Parse(sample)
	if !r.OK() {
		for _, e := range r.Errors {
			t.Logf("error: %v", e)
		}
		t.Fatalf("got %d errors", len(r.Errors))
	}
	f := r.File
	if f.Module.String() != "game::demo" {
		t.Fatalf("module = %q", f.Module.String())
	}
	if len(f.Imports) != 2 || f.Imports[0].Alias != "math" {
		t.Fatalf("imports: %+v", f.Imports)
	}
	kinds := map[string]int{}
	for _, d := range f.Decls {
		switch d.(type) {
		case *ComponentDecl:
			kinds["component"]++
		case *StructDecl:
			kinds["struct"]++
		case *EnumDecl:
			kinds["enum"]++
		case *ConstDecl:
			kinds["const"]++
		case *TypeAliasDecl:
			kinds["type"]++
		case *EntityDecl:
			kinds["entity"]++
		case *EventDecl:
			kinds["event"]++
		case *ResourceDecl:
			kinds["resource"]++
		case *SystemDecl:
			kinds["system"]++
		case *ShaderDecl:
			kinds["shader"]++
		case *PipelineDecl:
			kinds["pipeline"]++
		case *InterfaceDecl:
			kinds["interface"]++
		case *ExtendDecl:
			kinds["extend"]++
		case *ExternDecl:
			kinds["extern"]++
		}
	}
	for _, k := range []string{"component", "struct", "enum", "const", "type", "entity", "event", "resource", "system", "shader", "pipeline", "interface", "extend", "extern"} {
		if kinds[k] == 0 {
			t.Errorf("missing decl kind %s (have %v)", k, kinds)
		}
	}
	if kinds["component"] != 2 || kinds["system"] != 2 {
		t.Errorf("counts: %v", kinds)
	}
}

func TestNGRuleCoverage(t *testing.T) {
	var missing []string
	for _, rule := range NGRules() {
		if _, ok := NGRuleCoverage[rule]; !ok {
			missing = append(missing, rule)
		}
	}
	if len(missing) > 0 {
		t.Fatalf("ng rules without Go coverage: %s", strings.Join(missing, ", "))
	}
}

func TestParseErrors(t *testing.T) {
	for _, src := range []string{
		`component {`,               // missing name
		`fn broken( { }`,            // bad params
		`system S { bogus member }`, // bad member
		`let = ;`,                   // bad var
	} {
		r := Parse(src)
		if r.OK() {
			t.Errorf("expected errors for %q", src)
		}
	}
}

func TestExprPrecedence(t *testing.T) {
	e, errs := ParseExpression(`1 + 2 * 3 - 4 / 2`)
	if len(errs) != 0 {
		t.Fatalf("errs: %v", errs)
	}
	// ((1 + (2*3)) - (4/2))
	sub, ok := e.(*BinaryExpr)
	if !ok || sub.Op != "-" {
		t.Fatalf("root should be `-`: %#v", e)
	}
}

// TestControlFlowBraces guards the struct-literal vs block disambiguation:
// `Path {` after an expression opens a block for control flow unless it is
// an adjacent brace or an uppercase type with field syntax. Regression test
// for a non-transactional terminal bug that hung the parser.
func TestControlFlowBraces(t *testing.T) {
	for _, src := range []string{
		`fn f() { match x { 1 => { g(); }, _ => {} } }`,
		`fn f() { for each (&mut Position as p, &Velocity as v) in world { p.x = 1; } }`,
		`fn f() { for p in items { p; } }`,
		`fn f() { while x { g(); } }`,
		`fn f() { if x { g(); } }`,
		`fn f() { if Foo { g(); } }`,
		`fn f() { g(Damage{ amount: 10 }); }`,
		`fn f() { let d = Damage { amount: 10 }; }`,
		`fn f() { let b = true; let n = null; }`,
		`fn f() { p.x = p.x + 1; }`,
		`fn f() { let x = if c { 1 } else { 2 }; }`,
		`pipeline P { blend { enabled: true; } }`,
	} {
		r := Parse(src)
		if !r.OK() {
			t.Errorf("%s: %v", src, r.Errors)
		}
	}
}

func TestWalkCounts(t *testing.T) {
	r := Parse(sample)
	if !r.OK() {
		t.Skip("sample must parse first")
	}
	c := &counter{}
	Walk(c, r.File)
	if c.decls < 15 || c.exprs == 0 || c.stmts == 0 {
		t.Errorf("walk counts too low: %+v", c)
	}
}

type counter struct {
	BaseVisitor
	decls, exprs, stmts int
}

func (c *counter) VisitDecl(Decl) bool  { c.decls++; return true }
func (c *counter) VisitExpr(Expr) bool  { c.exprs++; return true }
func (c *counter) VisitStmt(Stmt) bool  { c.stmts++; return true }
