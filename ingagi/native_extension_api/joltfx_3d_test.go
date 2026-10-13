package native_extension_api

import (
	"os"
	"testing"
)

// stubScene mimics the desktop's shared 3D scene behind the scalar ABI:
// monotonic object indices, recorded key/color/cloner writes.
type stubScene struct {
	next   int64
	keys   int
	colors int
	cloner int
	real   int
}

func (s *stubScene) num(v Value) float64 {
	if v.Kind == Float {
		return v.Real
	}
	return float64(v.Integer)
}

func (s *stubScene) functions() []Function {
	intv := func(n int64) Value { return Value{Kind: Int, Integer: n} }
	return []Function{
		{Name: "engine_time", Arity: 0, Call: func([]Value) (Value, error) {
			return Value{Kind: Float, Real: 0.5}, nil
		}},
		{Name: "engine_scene_count", Arity: 0, Call: func([]Value) (Value, error) {
			return intv(s.next), nil
		}},
		{Name: "engine_scene_add", Arity: 1, Call: func(a []Value) (Value, error) {
			if a[0].Kind != Int || a[0].Integer < 0 || a[0].Integer > 16 {
				return Value{}, fail(InvalidArgument, "primitive")
			}
			id := s.next
			s.next++
			return intv(id), nil
		}},
		{Name: "engine_scene_remove", Arity: 1, Call: func([]Value) (Value, error) {
			return Value{}, nil
		}},
		{Name: "engine_scene_get", Arity: 2, Call: func([]Value) (Value, error) {
			return Value{Kind: Float, Real: 1.5}, nil
		}},
		{Name: "engine_scene_sample", Arity: 3, Call: func([]Value) (Value, error) {
			return Value{Kind: Float, Real: 2.5}, nil
		}},
		{Name: "engine_scene_set", Arity: 3, Call: func([]Value) (Value, error) {
			s.keys++
			return Value{}, nil
		}},
		{Name: "engine_scene_key", Arity: 4, Call: func([]Value) (Value, error) {
			s.keys++
			return Value{}, nil
		}},
		{Name: "engine_scene_cloner", Arity: 4, Call: func(a []Value) (Value, error) {
			if a[1].Integer < 0 || a[1].Integer > 3 {
				return Value{}, fail(InvalidArgument, "mode")
			}
			s.cloner++
			return Value{}, nil
		}},
		{Name: "engine_scene_cloner_real", Arity: 1, Call: func([]Value) (Value, error) {
			s.real++
			return intv(s.next), nil
		}},
		{Name: "engine_scene_procedural", Arity: 2, Call: func(a []Value) (Value, error) {
			if a[1].Integer == 1 {
				return intv(6), nil
			}
			return intv(0), nil
		}},
		{Name: "engine_scene_color", Arity: 3, Call: func(a []Value) (Value, error) {
			if a[1].Integer < 0 || a[1].Integer > 2 || s.num(a[2]) < 0 || s.num(a[2]) > 1 {
				return Value{}, fail(InvalidArgument, "color")
			}
			s.colors++
			return Value{}, nil
		}},
	}
}

func loadFile(t *testing.T, s *Session, path string) {
	t.Helper()
	src, err := os.ReadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if err := s.Load(string(src)); err != nil {
		t.Fatalf("%s: %v", path, err)
	}
}

// TestJoltfx3DDrives executes every shipped 3D-tab drive against the stub
// scene, proving the stdlib/examples run end to end (not just parse).
func TestJoltfx3DDrives(t *testing.T) {
	drives := map[string]struct {
		entry string
		want  int64
	}{
		"../examples/joltfx_3d_drive.ing":                  {entry: "drive", want: 6},
		"../examples/joltfx_iap_pack.ing":                  {entry: "drive", want: 1},
		"../stdlib/ecs/joltfx_scene_ecs.ing":               {entry: "main", want: 1},
		"../stdlib/shader/joltfx_material.ing":             {entry: "main", want: 0},
		"../stdlib/gameplay-animation-ui/joltfx_drive.ing": {entry: "drive", want: 1},
		"../stdlib/assets-resources/joltfx_iap.ing":        {entry: "drive", want: 1},
	}
	for path, tc := range drives {
		if _, err := os.Stat(path); err != nil {
			t.Skipf("%s missing: %v", path, err)
		}
		scene := &stubScene{}
		s := New(100000)
		if err := s.Register(scene.functions()); err != nil {
			t.Fatal(err)
		}
		loadFile(t, s, path)
		v, err := s.Call(tc.entry, nil)
		if err != nil {
			t.Fatalf("%s %s: %v", path, tc.entry, err)
		}
		if v.Integer != tc.want {
			t.Fatalf("%s %s = %d, want %d", path, tc.entry, v.Integer, tc.want)
		}
		if scene.keys == 0 {
			t.Fatalf("%s wrote no keys", path)
		}
		if err := s.Close(); err != nil {
			t.Fatal(err)
		}
	}
}
