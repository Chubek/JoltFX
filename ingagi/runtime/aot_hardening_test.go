package runtime

import (
	"encoding/binary"
	"strings"
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// encodeSample builds an image with populated pools, code, entries, globals,
// type metadata and system hooks. Its decoded entry exercises aggregates,
// closures and math to check that the restored code remains usable.
func encodeSample(t *testing.T) []byte {
	t.Helper()
	src := `
struct P { x: i32; y: i32; }
enum State { Idle, Busy }
const K: i32 = 6;
struct Damage { amount: i32; }
fn helper(a: i32) -> i32 { return a + 1; }
fn main() {
    let p = P { x: 1, y: 2 };
    let s = match State::Busy { State::Idle => 0, _ => 1 };
    let arr = [1, 2, 3];
    let f = 1.5;
    let v = vec3(1.0, 2.0, 3.0);
    let c = |x: i32| { return helper(x); };
    return K * 7 + s + arr[0] + helper(1);
}
system Render {
    update(dt: f32) { helper(2); }
    on_event(e: Damage) { helper(3); }
}
`
	r := fe.Parse(src)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		t.Fatal(err)
	}
	img, err := vm.Encode()
	if err != nil {
		t.Fatal(err)
	}
	return img
}

// decodeOrFail requires Decode to reject data.
func decodeOrFail(t *testing.T, data []byte, want string) {
	t.Helper()
	d, err := Decode(data)
	if err == nil {
		t.Fatalf("Decode accepted %d bytes (module %v)", len(data), d)
	}
	if !strings.Contains(err.Error(), want) {
		t.Fatalf("error %q does not mention %q", err, want)
	}
}

// mutateImage copies src and rewrites the little-endian u64 at offset.
func setU64(t *testing.T, src []byte, offset int, v uint64) []byte {
	t.Helper()
	out := append([]byte(nil), src...)
	if offset+8 > len(out) {
		t.Fatalf("offset %d beyond image of %d bytes", offset, len(out))
	}
	binary.LittleEndian.PutUint64(out[offset:], v)
	return out
}

func TestDecodeRejectsTruncatedImages(t *testing.T) {
	img := encodeSample(t)
	// Every prefix must be rejected, never panic.
	for n := 0; n < len(img); n++ {
		func() {
			defer func() {
				if p := recover(); p != nil {
					t.Fatalf("Decode panicked on %d-byte prefix: %v", n, p)
				}
			}()
			if d, err := Decode(img[:n]); err == nil && d != nil {
				t.Fatalf("Decode accepted a %d-byte prefix", n)
			}
		}()
	}
}

func TestDecodeRejectsTrailingBytes(t *testing.T) {
	img := encodeSample(t)
	decodeOrFail(t, append(append([]byte(nil), img...), 0), "trailing")
}

func TestDecodeRejectsBadMagicAndVersion(t *testing.T) {
	img := encodeSample(t)
	decodeOrFail(t, []byte("NOTINGAGI\x00"), "magic")
	decodeOrFail(t, []byte("short"), "too short")
	bad := append([]byte(nil), img...)
	bad[0] = 'X'
	decodeOrFail(t, bad, "magic")
	// Version lives right after the 6-byte magic.
	decodeOrFail(t, setU64(t, img, len(aotMagic), aotVersion+1), "version")
}

// sectionOffsets walks the pool header and locates its collection lengths.
func sectionOffsets(t *testing.T, img []byte) map[string]int {
	t.Helper()
	u64 := func(at int) uint64 {
		if at+8 > len(img) {
			t.Fatalf("offset %d beyond image", at)
		}
		return binary.LittleEndian.Uint64(img[at:])
	}
	off := map[string]int{}
	off["ints"] = len(aotMagic) + 8
	pos := off["ints"] + 8 + 8*int(u64(off["ints"]))
	off["floats"] = pos
	pos += 8 + 8*int(u64(pos))
	off["strings"] = pos
	// Skip the string bytes themselves to reach the code count.
	nStr := int(u64(pos))
	pos += 8
	for i := 0; i < nStr; i++ {
		n := int(u64(pos))
		pos += 8 + n
	}
	off["code"] = pos
	return off
}

// TestDecodeRejectsAllocationBombs feeds oversized counts where a
// non-validating reader would allocate gigabytes from a small input.
func TestDecodeRejectsAllocationBombs(t *testing.T) {
	img := encodeSample(t)
	off := sectionOffsets(t, img)
	for _, bomb := range []uint64{^uint64(0), 1 << 40, 1 << 32, 1 << 24, 1 << 20} {
		decodeOrFail(t, setU64(t, img, off["ints"], bomb), "int pool")
		decodeOrFail(t, setU64(t, img, off["floats"], bomb), "float pool")
		decodeOrFail(t, setU64(t, img, off["strings"], bomb), "string list")
		decodeOrFail(t, setU64(t, img, off["code"], bomb), "code")
	}
}

// TestDecodeRejectsDeepNesting builds an image whose global array nests past
// the depth limit. readValue recurses, so this is also a stack guard.
func TestDecodeRejectsDeepNesting(t *testing.T) {
	// Drive depth through a purpose-built image rather than locating the
	// global value map inside a real one.
	var w aotWriter
	w.b.WriteString(aotMagic)
	w.u64(aotVersion)
	w.u64(0)    // ints
	w.u64(0)    // floats
	w.strs(nil) // strings
	w.u64(0)    // code
	w.u64(0)    // entries
	w.u64(1)    // one global
	w.str("deep")
	for i := 0; i < aotMaxDepth+50; i++ {
		w.b.WriteByte(vArray)
		w.u64(1) // one element each
	}
	w.b.WriteByte(vNil)
	w.u64(0) // types
	w.u64(0) // hooks
	for i := 0; i < 5; i++ {
		w.strs(nil) // systems, prefabs, shaders, pipelines, events
	}
	decodeOrFail(t, w.b.Bytes(), "nests deeper")
}

func TestDecodeRejectsBadValueTags(t *testing.T) {
	var w aotWriter
	w.b.WriteString(aotMagic)
	w.u64(aotVersion)
	w.u64(0)
	w.u64(0)
	w.strs(nil)
	w.u64(0)
	w.u64(0)
	w.u64(1)
	w.str("bad")
	w.b.WriteByte(0xfe) // not a value tag
	decodeOrFail(t, w.b.Bytes(), "value tag")
}

// TestDecodeRejectsInvalidTapes checks the Validate gate: an image whose code
// jumps outside the stream must not install.
func TestDecodeRejectsInvalidTapes(t *testing.T) {
	// Build a purpose-made image: one Halt plus one jump.
	build := func(jumpTarget int64) []byte {
		var w aotWriter
		w.b.WriteString(aotMagic)
		w.u64(aotVersion)
		w.u64(0)
		w.u64(0)
		w.strs(nil)
		w.u64(2) // two instructions
		w.b.WriteByte(byte(OpHalt))
		w.i64(0)
		w.i64(0)
		w.i64(0)
		w.b.WriteByte(byte(OpJump))
		w.i64(jumpTarget)
		w.i64(0)
		w.i64(0)
		w.u64(1)
		w.str("main")
		w.u64(0)
		w.u64(0)
		w.u64(0) // globals
		w.u64(0) // types
		w.u64(0) // hooks
		for i := 0; i < 5; i++ {
			w.strs(nil)
		}
		return w.b.Bytes()
	}
	// Target inside the stream decodes; far outside it is rejected.
	if _, err := Decode(build(1)); err != nil {
		t.Fatalf("in-range jump rejected: %v", err)
	}
	decodeOrFail(t, build(99), "outside pool")

	// An unknown opcode is rejected by Validate, before execution.
	var w aotWriter
	w.b.WriteString(aotMagic)
	w.u64(aotVersion)
	w.u64(0)
	w.u64(0)
	w.strs(nil)
	w.u64(1)
	w.b.WriteByte(250) // no handler
	w.i64(0)
	w.i64(0)
	w.i64(0)
	w.u64(0) // entries
	w.u64(0) // globals
	w.u64(0) // types
	w.u64(0) // hooks
	for i := 0; i < 5; i++ {
		w.strs(nil)
	}
	decodeOrFail(t, w.b.Bytes(), "unknown opcode")

	// A pool index outside the pool is rejected too.
	var w2 aotWriter
	w2.b.WriteString(aotMagic)
	w2.u64(aotVersion)
	w2.u64(0)
	w2.u64(0)
	w2.strs(nil)
	w2.u64(1)
	w2.b.WriteByte(byte(OpPushString))
	w2.i64(0)
	w2.i64(0)
	w2.i64(7) // only zero strings exist
	w2.u64(0) // entries
	w2.u64(0) // globals
	w2.u64(0) // types
	w2.u64(0) // hooks
	for i := 0; i < 5; i++ {
		w2.strs(nil)
	}
	decodeOrFail(t, w2.b.Bytes(), "string index")
}

func TestDecodeRoundTripPreservesGlobals(t *testing.T) {
	img := encodeSample(t)
	d, err := Decode(img)
	if err != nil {
		t.Fatal(err)
	}
	vm := New()
	if err := vm.LoadDecoded(d); err != nil {
		t.Fatal(err)
	}
	// The decoded module must still run its entry point.
	if v, err := vm.CallBudget(Budget{MaxInstrs: 100000}, "main"); err != nil || v.I != 46 {
		t.Fatalf("decoded entry returned %v: %v", v, err)
	}
}

// TestLoadModuleRejectsInvalidTape confirms the in-process path is gated too,
// and that a rejected load leaves the previous module working.
func TestLoadModuleRejectsInvalidTape(t *testing.T) {
	good := compileMain(t, `fn main() { return 42; }`)
	vm := New()
	if err := vm.LoadModule(good); err != nil {
		t.Fatal(err)
	}
	broken := compileMain(t, `fn main() { print("x"); return 42; }`)
	// Corrupt a string operand to point past the pool.
	corrupted := false
	for i := range broken.Tape.Code {
		if broken.Tape.Code[i].Op == OpPushString {
			broken.Tape.Code[i].C = 9999
			corrupted = true
		}
	}
	if !corrupted {
		t.Fatal("expected the sample to contain a string push")
	}
	if err := vm.LoadModule(broken); err == nil {
		t.Fatal("LoadModule accepted an out-of-range pool index")
	}
	// The previously loaded module must still work.
	v, err := vm.CallBudget(Budget{MaxInstrs: 10000}, "main")
	if err != nil || v.I != 42 {
		t.Fatalf("previous module broken: %v %v", v, err)
	}
}

func compileMain(t *testing.T, src string) *Module {
	t.Helper()
	r := fe.Parse(src)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	m, errs := Compile(r.File)
	if len(errs) != 0 {
		t.Fatal(errs)
	}
	return m
}

// TestUnknownOpcodeRejectedAtLoad checks the Validate gate catches an opcode
// with no handler before any code runs.
func TestUnknownOpcodeRejectedAtLoad(t *testing.T) {
	m := compileMain(t, `fn main() { return 1; }`)
	m.Tape.Code[0].Op = 254 // no handler
	if err := New().LoadModule(m); err == nil {
		t.Fatal("LoadModule accepted an opcode with no handler")
	}
}

// TestDispatchGuardsUnknownOpcode exercises the run() guard directly by
// installing a tape that skips validation, which is how a host embedding that
// mutates its own module could reach the dispatch loop.
func TestDispatchGuardsUnknownOpcode(t *testing.T) {
	m := compileMain(t, `fn main() { return 1; }`)
	m.Tape.Code[0].Op = 254
	vm := New()
	// Bypass Validate by installing the state directly, then run.
	vm.tape = m.Tape
	vm.funcs = map[string]int{"main": 0}
	vm.params = map[string]int{"main": 0}
	if err := vm.frames.push(frame{locals: []Value{}}); err != nil {
		t.Fatal(err)
	}
	if _, err := vm.run(0, Budget{MaxInstrs: 100}); err == nil {
		t.Fatal("run executed an opcode with no handler")
	} else if !strings.Contains(err.Error(), "unknown opcode") {
		t.Fatalf("unexpected error: %v", err)
	}
}

// TestValidateCoversOperandClasses checks each operand rule reports a located
// diagnostic rather than passing.
func TestValidateCoversOperandClasses(t *testing.T) {
	pool := NewConstPool()
	pool.InternInt(1)
	pool.InternFloat(1)
	pool.InternString("")
	base := func(op Op, a, b, c int32) *Module {
		return &Module{Tape: &Tape{Code: []Instr{{Op: op, A: a, B: b, C: c}, {Op: OpHalt}}, Pool: pool}, Params: map[string]int{}}
	}
	for _, tc := range []struct {
		name string
		in   Instr
		want string
	}{
		{"int index", Instr{Op: OpPushInt, C: 5}, "int index"},
		{"float index", Instr{Op: OpPushFloat, C: 5}, "float index"},
		{"string index", Instr{Op: OpLoadGlobal, C: 9}, "string index"},
		{"target", Instr{Op: OpJump, A: 40}, "target index"},
		{"negative target", Instr{Op: OpJump, A: -1}, "target index"},
		{"negative count", Instr{Op: OpMakeArray, A: -1}, "negative operand"},
		{"negative slot", Instr{Op: OpLoadLocal, A: -3}, "negative operand"},
		{"bad flag", Instr{Op: OpReturn, A: 7}, "flag 7"},
		{"observer kind", Instr{Op: OpObserve, A: 9}, "observer kind"},
		{"observer name", Instr{Op: OpObserve, C: 9}, "string index"},
		{"unknown opcode", Instr{Op: 254}, "unknown opcode"},
	} {
		m := &Module{Tape: &Tape{Code: []Instr{tc.in, {Op: OpHalt}}, Pool: pool}, Params: map[string]int{}}
		err := Validate(m)
		if err == nil || !strings.Contains(err.Error(), tc.want) {
			t.Errorf("%s: got %v, want %q", tc.name, err, tc.want)
		}
	}
	// A well-formed module passes.
	if err := Validate(base(OpPushInt, 0, 0, 0)); err != nil {
		t.Fatalf("valid module rejected: %v", err)
	}
	// Entry points outside the code are rejected.
	m := base(OpHalt, 0, 0, 0)
	m.Tape.Entry = map[string]int{"f": 99}
	if err := Validate(m); err == nil || !strings.Contains(err.Error(), "outside 2 instructions") {
		t.Fatalf("entry bounds: %v", err)
	}
	// A nil module is rejected rather than panicking.
	if err := Validate(nil); err == nil {
		t.Fatal("Validate accepted nil")
	}
	if err := Validate(&Module{}); err == nil {
		t.Fatal("Validate accepted a module with no tape")
	}
}

// TestValidateCoversEveryOpcode asserts every defined opcode has both a
// handler and an operand rule, so a new instruction cannot be added without
// both.
func TestValidateCoversEveryOpcode(t *testing.T) {
	for op := OpPushNil; op <= OpHalt; op++ {
		if threadedTable[op] == nil {
			t.Fatalf("opcode %s has no handler", OpName(op))
		}
		if opOperands[op] == unclassified {
			t.Errorf("opcode %s has no operand rule", OpName(op))
		}
	}
}

func TestFuzzDecodeNeverPanics(t *testing.T) {
	img := encodeSample(t)
	// Deterministic single-iteration fuzz used as a plain test so it runs
	// without -fuzz; the same function is registered with Fuzz below.
	f := func(t *testing.T, data []byte) {
		_, _ = Decode(data)
	}
	f(t, img)
	f(t, img[:len(img)/2])
	f(t, []byte{aotMagic[0]})
	for _, seed := range [][]byte{nil, {}, {0}, {0xff}, []byte(aotMagic)} {
		f(t, seed)
	}
}

func FuzzDecode(f *testing.F) {
	f.Add(encodeSampleForFuzz(f))
	f.Add([]byte(aotMagic))
	f.Add([]byte{})
	f.Fuzz(func(t *testing.T, data []byte) {
		d, err := Decode(data)
		if err != nil {
			return
		}
		// Structural validation is not a full bytecode verifier. Test decoding
		// and installation here; execution has separate bounded regressions.
		vm := New()
		if err := vm.LoadDecoded(d); err != nil {
			return
		}
	})
}

func encodeSampleForFuzz(f *testing.F) []byte {
	src := `fn main() { let a = [1,2]; return 7; }`
	r := fe.Parse(src)
	m, _ := Compile(r.File)
	vm := New()
	if err := vm.LoadModule(m); err != nil {
		f.Fatal(err)
	}
	img, err := vm.Encode()
	if err != nil {
		f.Fatal(err)
	}
	return img
}

// TestPoolAccessorsAreBoundsSafe covers the hardened pool accessors directly.
func TestPoolAccessorsAreBoundsSafe(t *testing.T) {
	p := NewConstPool()
	p.InternInt(5)
	p.InternString("x")
	if got := p.Int(-1); got != 0 {
		t.Fatalf("Int(-1) = %d", got)
	}
	if got := p.Int(5); got != 0 {
		t.Fatalf("Int(5) = %d", got)
	}
	if got := p.String(-1); got != "" {
		t.Fatalf("String(-1) = %q", got)
	}
	if got := p.String(3); got != "" {
		t.Fatalf("String(3) = %q", got)
	}
	if got := p.Float(0); got != 0 {
		t.Fatalf("Float(0) = %v", got)
	}
	if got := p.Int(0); got != 5 {
		t.Fatalf("Int(0) = %d", got)
	}
	if got := p.String(0); got != "x" {
		t.Fatalf("String(0) = %q", got)
	}
}
