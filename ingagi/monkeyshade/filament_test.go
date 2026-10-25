package shader

import (
	"bytes"
	"context"
	"encoding/binary"
	"encoding/json"
	"errors"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"
	"time"
)

const filamentTest = `shader Surface(target: "filament", shading_model: "lit", requires: "uv0", double_sided: true) {
 uniform SurfaceParams { tint: vec4; roughness: float; weights: [float; 2]; } params;
 texture2d albedo;
 const Gain: float = 0.8;
 fn shade(color: vec4) -> vec4 { return color * Gain; }
 interface Varyings { @location(0) tint: vec4; }
 vertex(outputs: Varyings) {
   outputs.tint = params.tint;
   material.worldPosition.x += 0.1;
 }
 fragment(inputs: Varyings) {
   prepareMaterial(material);
   material.baseColor = shade(texture(albedo, getUV0())) * inputs.tint;
   material.roughness = params.roughness;
 }
}`

func TestFilamentMaterial(t *testing.T) {
	s := parseShaderTest(t, filamentTest)
	before, _ := json.Marshal(s)
	text, err := CompileFilament(s)
	if err != nil {
		t.Fatal(err)
	}
	for _, want := range []string{"shadingModel : lit", "doubleSided : true", "requires : [uv0]", "type : float4, name : SurfaceParams_tint", "type : float[2]", "type : sampler2d, name : albedo", "void materialVertex(inout MaterialVertexInputs material)", "void material(inout MaterialInputs material)", "materialParams.SurfaceParams_tint", "materialParams_albedo", "variable_ig_filament_v0", "material.ig_filament_v0"} {
		if !strings.Contains(text, want) {
			t.Fatalf("missing %q:\n%s", want, text)
		}
	}
	if strings.Contains(text, "#version") || strings.Contains(text, "layout(set") || strings.Contains(text, "void main") {
		t.Fatal(text)
	}
	r, err := ReflectFilament(s)
	if err != nil || len(r.Parameters) != 4 || r.Parameters[0].Source != "params.tint" || r.Parameters[0].Name != "SurfaceParams_tint" {
		t.Fatalf("%+v: %v", r, err)
	}
	for _, format := range []string{"filament", "reflect"} {
		data, err := CompileArtifact(context.Background(), s, "material", format, ToolOptions{GLSLang: "/missing", Matc: "/missing"})
		if err != nil || len(data) == 0 {
			t.Fatalf("%s: %v", format, err)
		}
	}
	after, _ := json.Marshal(s)
	if !bytes.Equal(before, after) {
		t.Fatal("lowering mutated source AST")
	}
	if _, err := CompileGLSL(s, "fragment"); err == nil {
		t.Fatal("material profile accepted as standalone GLSL")
	}
}

func TestFilamentRejectsUnsupported(t *testing.T) {
	for _, source := range []string{
		`shader Bad { fragment {} }`,
		`shader Bad(target:"filament") { compute {} }`,
		`shader Bad(target:"filament") { vertex {} }`,
		`shader Bad(target:"filament",wat:true) { fragment {} }`,
		`shader Bad(target:"filament",target:"filament") { fragment {} }`,
		`shader Bad(target:"filament",shading_model:"oops") { fragment {} }`,
		`shader Bad(target:"filament",requires:"uv0,bogus") { fragment {} }`,
		`shader Bad(target:"filament") { specialize X:float=1.0; fragment {} }`,
		`shader Bad(target:"filament") { storage buffer B { x:float; }; fragment {} }`,
		`shader Bad(target:"filament") { @binding(0) texture2d t; fragment {} }`,
		`shader Bad(target:"filament") { uniform B { x:mat2; }; fragment {} }`,
		`shader Bad(target:"filament") { fragment { gl_FragDepth=0.5; } }`,
		`shader Bad(target:"filament") { fragment { var material:float=1.0; } }`,
		`shader Bad(target:"filament") { uniform B { x:float; } b; fragment { b.x=1.0; } }`,
		`shader Bad(target:"filament") { uniform B { x:float; } b; fragment { var b:float=1.0; } }`,
		`shader Bad(target:"filament") { uniform B { x:float; }; fn f(x:float)->float{return x;} fragment {} }`,
		`shader Bad(target:"filament") { fragment { materialParams.x=1.0; } }`,
		`shader Bad(target:"filament") { fragment { material.ig_filament_v0=vec4(1.0); } }`,
		`shader Bad(target:"filament") { uniform A_B { c:float; } ab; uniform A { B_c:float; } a; fragment {} }`,
		`shader Bad(target:"filament") { interface O{@location(0) color:vec4;} fragment(outputs:O){} }`,
		`shader Bad(target:"filament") { interface V{@location(4) v:vec4;} vertex(outputs:V){} fragment(inputs:V){} }`,
		`shader Bad(target:"filament") { interface V{@location(0) v:vec3;} vertex(outputs:V){} fragment(inputs:V){} }`,
		`shader Bad(target:"filament") { interface V{@location(0) v:vec4;} fragment(inputs:V){} }`,
		`shader Bad(target:"filament") { fragment { defer {} } }`,
	} {
		s := parseShaderTest(t, source)
		if text, err := CompileFilament(s); err == nil || text != "" {
			t.Fatalf("accepted %s: %s (%v)", source, text, err)
		}
	}
	if _, err := CompileFilament(nil); err == nil {
		t.Fatal("nil shader accepted")
	}
	s := parseShaderTest(t, filamentTest)
	if _, err := CompileArtifact(context.Background(), s, "fragment", "filament", ToolOptions{}); err == nil {
		t.Fatal("silently selected part of material")
	}
	if _, err := CompileFilamat(nil, s, ToolOptions{}); err == nil {
		t.Fatal("nil context accepted")
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := CompileFilamat(ctx, s, ToolOptions{}); err == nil {
		t.Fatal("cancelled context accepted")
	}
	if _, err := CompileFilamat(context.Background(), s, ToolOptions{Matc: "/missing/matc"}); err == nil {
		t.Fatal("missing tool accepted")
	}
}

func TestFilamentPackageFraming(t *testing.T) {
	chunk := func(tag uint64, payload []byte) []byte {
		b := make([]byte, 12+len(payload))
		binary.LittleEndian.PutUint64(b, tag)
		binary.LittleEndian.PutUint32(b[8:], uint32(len(payload)))
		copy(b[12:], payload)
		return b
	}
	version := chunk(0x4d41545f56455253, []byte{1, 0, 0, 0})
	code := chunk(0x4d41545f474c534c, []byte{1})
	valid := append(append([]byte{}, version...), code...)
	if !validFilamat(valid) {
		t.Fatal("valid chunk framing rejected")
	}
	for _, data := range [][]byte{nil, []byte("not a package"), version, code, valid[:len(valid)-1], append(append([]byte{}, valid...), 1), append(append([]byte{}, valid...), version...)} {
		if validFilamat(data) {
			t.Fatalf("accepted malformed package %x", data)
		}
	}
}

func TestFilamentToolFailures(t *testing.T) {
	if runtime.GOOS == "windows" {
		t.Skip("POSIX process fixtures")
	}
	s := parseShaderTest(t, filamentTest)
	dir := t.TempDir()
	tool := filepath.Join(dir, "fake matc")
	write := func(body string) {
		t.Helper()
		if err := os.WriteFile(tool, []byte("#!/bin/sh\n"+body), 0700); err != nil {
			t.Fatal(err)
		}
	}
	write("while [ $# -gt 0 ]; do if [ \"$1\" = --output ]; then shift; printf garbage > \"$1\"; exit 0; fi; shift; done\n")
	if data, err := CompileFilamat(context.Background(), s, ToolOptions{Matc: tool}); err == nil || len(data) != 0 {
		t.Fatal("malformed tool output accepted")
	}
	write("echo material-error >&2; exit 1\n")
	if data, err := CompileFilamat(context.Background(), s, ToolOptions{Matc: tool}); err == nil || !strings.Contains(err.Error(), "material-error") || len(data) != 0 {
		t.Fatalf("missing diagnostics: %v", err)
	}
	write("exec sleep 5\n")
	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()
	if _, err := CompileFilamat(ctx, s, ToolOptions{Matc: tool}); !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("timeout not propagated: %v", err)
	}
	for _, opts := range []ToolOptions{{FilamentAPI: "bad"}, {FilamentPlatform: "bad"}} {
		if _, err := CompileFilamat(context.Background(), s, opts); err == nil {
			t.Fatal("invalid target accepted")
		}
	}
}

func TestFilamentMatc(t *testing.T) {
	matc := os.Getenv("INGAGI_MATC")
	if matc == "" {
		var err error
		matc, err = exec.LookPath("matc")
		if err != nil {
			t.Skip("set INGAGI_MATC to the vendored matc for real package tests")
		}
	}
	ctx, cancel := context.WithTimeout(context.Background(), 25*time.Second)
	defer cancel()
	for _, api := range []string{"opengl", "vulkan", "metal"} {
		data, err := CompileFilamat(ctx, parseShaderTest(t, filamentTest), ToolOptions{Matc: matc, FilamentAPI: api, FilamentPlatform: "mobile"})
		if err != nil {
			t.Fatalf("%s: %v", api, err)
		}
		if len(data) < 100 {
			t.Fatalf("%s: empty material package", api)
		}
	}
	// Exercise unaliased values, vectors/matrices/arrays, sampler dimensions and
	// the shared struct/control-flow body emitter through a desktop material.
	more := parseShaderTest(t, `shader More(target:"filament",shading_model:"unlit") {
 uniform Values { tint:vec4; enabled:bool; transform:mat4; weights:[vec3;2]; indices:ivec2; mask:uvec4; normalMatrix:mat3; };
 texture_cube environment;
 texture2d_array layers;
 struct Tint { value:vec4; }
 fn applyTint(v:vec4)->vec4 {
   var result:Tint; result.value=v;
   var i:int=0;
   while(i<2) {result.value*=0.5;i+=1;}
   return result.value;
 }
 fragment {prepareMaterial(material);material.baseColor=enabled?applyTint(transform*tint):vec4(0.0);}
}`)
	if _, err := CompileFilamat(ctx, more, ToolOptions{Matc: matc, FilamentPlatform: "desktop"}); err != nil {
		t.Fatal(err)
	}
	// Ask the real tool to independently reflect the emitted parameter names/types.
	text, err := CompileFilament(parseShaderTest(t, filamentTest))
	if err != nil {
		t.Fatal(err)
	}
	file := filepath.Join(t.TempDir(), "surface.mat")
	if err = os.WriteFile(file, []byte(text), 0600); err != nil {
		t.Fatal(err)
	}
	out, err := exec.CommandContext(ctx, matc, "--reflect", "parameters", file).CombinedOutput()
	if err != nil || !bytes.Contains(out, []byte("SurfaceParams_tint")) || !bytes.Contains(out, []byte("albedo")) {
		t.Fatalf("%s: %v", out, err)
	}
	bad := parseShaderTest(t, `shader Bad(target:"filament") {fragment {prepareMaterial(material);material.baseColor=unknown_function();}}`)
	if data, err := CompileFilamat(ctx, bad, ToolOptions{Matc: matc}); err == nil || len(data) != 0 {
		t.Fatal("matc failure published an artifact")
	}
}
