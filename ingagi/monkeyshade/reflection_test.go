package shader

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"os/exec"
	"strings"
	"testing"
)

const bufferLayoutSource = `shader Layout {
 struct Item {position:vec3;weight:float;}
 @set(2) @binding(3) uniform Uniforms {a:float; b:vec3; c:float; values:[float;2]; matrix:mat2; item:Item;} u;
 @binding(7) read storage buffer Storage {values:[float;2]; matrix:mat2; item:Item;} data;
 compute(local_size_x:8,local_size_y:2){let x:float=u.a+data.values[0];}
 }`

func TestBufferReflection(t *testing.T) {
	s := parseShaderTest(t, bufferLayoutSource)
	r, err := Reflect(s, "compute")
	if err != nil {
		t.Fatal(err)
	}
	if r.LocalSize != [3]int{8, 2, 1} || len(r.Resources) != 2 {
		t.Fatalf("%+v", r)
	}
	u, b := r.Resources[0], r.Resources[1]
	if u.Set != 2 || u.Binding != 3 || u.Layout != "std140" || u.Size != 112 || u.Fields[1].Offset != 16 || u.Fields[2].Offset != 28 || u.Fields[3].ArrayStride != 16 || u.Fields[4].MatrixStride != 16 {
		t.Fatalf("std140: %+v", u)
	}
	if b.Layout != "std430" || b.Size != 48 || b.Access != "read" || b.Fields[0].ArrayStride != 4 || b.Fields[1].MatrixStride != 8 || b.Fields[2].Offset != 32 {
		t.Fatalf("std430: %+v", b)
	}
	// A caller may mutate reflection data without changing the AST or future queries.
	r.Resources[0].Fields[0].Name = "mutated"
	again, err := Reflect(s, "compute")
	if err != nil || again.Resources[0].Fields[0].Name != "a" {
		t.Fatal("reflection aliases state")
	}
	encoded, err := json.Marshal(again)
	if err != nil || !strings.Contains(string(encoded), `"binding":3`) {
		t.Fatal(err)
	}
}

func TestReflectionDepthBudgetWithMemoizedTypes(t *testing.T) {
	var source strings.Builder
	source.WriteString("shader S {struct T0 {value:float;}")
	for i := 1; i <= 32; i++ {
		fmt.Fprintf(&source, "struct T%d {value:T%d;}", i, i-1)
	}
	source.WriteString("uniform U {a:T31; b:T32;} u;compute{}}")
	if _, err := Reflect(parseShaderTest(t, source.String()), "compute"); err == nil {
		t.Fatal("memoized layout bypassed nesting limit")
	}
}

func FuzzInterfaceReflection(f *testing.F) {
	f.Add(uint8(0), uint8(1), uint8(0))
	f.Add(uint8(63), uint8(2), uint8(1))
	f.Fuzz(func(t *testing.T, location, count, kind uint8) {
		types := []string{"vec3", "mat4", "uint", "bool"}
		source := fmt.Sprintf("shader S {interface I {@location(%d) data:[%s;%d];} vertex(inputs:I){}}", location, types[int(kind)%len(types)], count)
		s := parseShaderTest(t, source)
		r, err := Reflect(s, "vertex")
		if err != nil {
			return
		}
		for _, v := range r.Inputs {
			if v.Location < 0 || v.Locations <= 0 || v.Location+v.Locations > MaxInterfaceLocations {
				t.Fatal("invalid reflected range")
			}
		}
		if _, err := CompileGLSL(s, "vertex"); err != nil {
			t.Fatal("reflection/lowering disagree:", err)
		}
	})
}

func TestReflectionMatchesSPIRVDecorations(t *testing.T) {
	compiler, err := exec.LookPath("glslangValidator")
	if err != nil {
		t.Skip(err)
	}
	data, err := CompileSPIRV(context.Background(), parseShaderTest(t, bufferLayoutSource), "compute", compiler)
	if err != nil {
		t.Fatal(err)
	}
	words := make([]uint32, len(data)/4)
	for i := range words {
		words[i] = binary.LittleEndian.Uint32(data[i*4:])
	}
	names := map[uint32]string{}
	offsets := map[[2]uint32]uint32{}
	matrixStrides := map[[2]uint32]uint32{}
	arrayStrides := map[uint32]uint32{}
	members := map[uint32][]uint32{}
	for at := 5; at < len(words); {
		n, op := int(words[at]>>16), words[at]&65535
		if n < 1 || at+n > len(words) {
			t.Fatal("malformed SPIR-V")
		}
		p := words[at : at+n]
		switch op {
		case 5: // OpName
			names[p[1]] = strings.TrimRight(string(data[(at+2)*4:(at+n)*4]), "\x00")
		case 30:
			members[p[1]] = p[2:] // OpTypeStruct
		case 71:
			if len(p) >= 4 && p[2] == 6 {
				arrayStrides[p[1]] = p[3]
			} // ArrayStride
		case 72:
			if len(p) >= 5 {
				key := [2]uint32{p[1], p[2]}
				if p[3] == 35 {
					offsets[key] = p[4]
				}
				if p[3] == 7 {
					matrixStrides[key] = p[4]
				}
			}
		}
		at += n
	}
	for name, want := range map[string][]uint32{"Uniforms": {0, 16, 28, 32, 64, 96}, "Storage": {0, 8, 32}} {
		var id uint32
		for candidate, n := range names {
			if n == name {
				id = candidate
			}
		}
		if id == 0 {
			t.Fatalf("missing block %s", name)
		}
		for i, offset := range want {
			if got, ok := offsets[[2]uint32{id, uint32(i)}]; !ok || got != offset {
				t.Fatalf("%s field %d: %d != %d", name, i, got, offset)
			}
		}
		arrayMember, matrixMember, stride, arrayStride := 0, 1, uint32(8), uint32(4)
		if name == "Uniforms" {
			arrayMember, matrixMember, stride, arrayStride = 3, 4, 16, 16
		}
		if arrayStrides[members[id][arrayMember]] != arrayStride {
			t.Fatal("buffer array stride")
		}
		if matrixStrides[[2]uint32{id, uint32(matrixMember)}] != stride {
			t.Fatal("matrix stride")
		}
	}
}

func TestReflectionRejectsInvalidLayouts(t *testing.T) {
	for _, source := range []string{
		`shader S {struct Loop {self:Loop;} uniform U {x:Loop;} u; compute{}}`,
		`shader S {uniform U {x:void;} u;compute{}}`,
		`shader S {uniform U {x:float;x:float;} u;compute{}}`,
		`shader S {@binding(0) uniform U {x:float;} u; @binding(0) sampler s;compute{}}`,
		`shader S {uniform U {x:[mat4;65536]; y:[mat4;65536];} u;compute{}}`,
	} {
		s := parseShaderTest(t, source)
		if _, err := Reflect(s, "compute"); err == nil {
			t.Fatalf("accepted %s", source)
		}
	}
	if _, err := Reflect(nil, "vertex"); err == nil {
		t.Fatal("nil shader")
	}
	c := Capabilities()
	if c.DeviceDispatch || c.MeshTaskStages {
		t.Fatal("advertised unimplemented GPU features")
	}
}
