package monkeytrace

import (
	_ "embed"
	"fmt"
	"io"

	"github.com/Chubek/JoltFX/ingagi"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

//go:embed demo.ing
var DemoSource string

// LoadScene executes import-free Ingagi source in a fresh VM and ECS world.
// Initializers and main each have a 100,000-instruction / 64-frame budget.
// The trace_* hosts author geometry/camera/lighting; returned entities work
// with ordinary Ingagi ECS builtins. Only a fully validated scene is returned.
func LoadScene(source string) (Scene, error) {
	if len(source) > 1024*1024 {
		return Scene{}, fmt.Errorf("monkeytrace: scene source exceeds 1 MiB")
	}
	module, err := ingagi.Compile(source)
	if err != nil {
		return Scene{}, err
	}
	scene := DefaultScene()
	vm := rt.New()
	vm.SetWorld(scene.World)
	vm.SetStdout(io.Discard)
	vm.SetBudget(rt.Budget{MaxInstrs: 100000, MaxFrames: 64})
	// All hosts have an exact vec3/numeric signature and synchronous ownership.
	host := func(name string, kinds []rt.Kind, call func([]rt.Value) (rt.Value, error)) {
		vm.RegisterHostFn(name, func(_ *rt.VM, args []rt.Value) (rt.Value, error) {
			if len(args) != len(kinds) {
				return rt.NilValue, fmt.Errorf("monkeytrace: %s expects %d arguments", name, len(kinds))
			}
			for i, k := range kinds {
				if k == rt.KFloat {
					if _, ok := args[i].AsFloat(); ok {
						continue
					}
				} else if args[i].K == k {
					continue
				}
				return rt.NilValue, fmt.Errorf("monkeytrace: %s argument %d must be %s", name, i+1, k)
			}
			return call(args)
		})
	}
	vec := func(v rt.Value) Vec3 { return Vec3{v.V[0], v.V[1], v.V[2]} }
	num := func(v rt.Value) float64 { n, _ := v.AsFloat(); return n }
	entity := func(index, gen uint32) rt.Value {
		return rt.Value{K: rt.KEntity, O: rt.EntityRef{Index: index, Gen: gen}}
	}
	host("trace_sphere", []rt.Kind{rt.KVec3, rt.KFloat, rt.KVec3, rt.KFloat, rt.KFloat}, func(a []rt.Value) (rt.Value, error) {
		if scene.World.Count() >= MaxPrimitives {
			return rt.NilValue, fmt.Errorf("monkeytrace: entity limit exceeded")
		}
		id := scene.AddSphere(Sphere{vec(a[0]), num(a[1])}, Material{vec(a[2]), num(a[3]), num(a[4])})
		return entity(id.Index, id.Gen), nil
	})
	host("trace_triangle", []rt.Kind{rt.KVec3, rt.KVec3, rt.KVec3, rt.KVec3, rt.KFloat, rt.KFloat}, func(a []rt.Value) (rt.Value, error) {
		if scene.World.Count() >= MaxPrimitives {
			return rt.NilValue, fmt.Errorf("monkeytrace: entity limit exceeded")
		}
		id := scene.AddTriangle(Triangle{vec(a[0]), vec(a[1]), vec(a[2])}, Material{vec(a[3]), num(a[4]), num(a[5])})
		return entity(id.Index, id.Gen), nil
	})
	host("trace_camera", []rt.Kind{rt.KVec3, rt.KVec3, rt.KVec3, rt.KFloat}, func(a []rt.Value) (rt.Value, error) {
		scene.Camera = Camera{vec(a[0]), vec(a[1]), vec(a[2]), num(a[3])}
		return rt.NilValue, nil
	})
	host("trace_sky", []rt.Kind{rt.KVec3, rt.KVec3}, func(a []rt.Value) (rt.Value, error) {
		scene.SkyLow, scene.SkyHigh = vec(a[0]), vec(a[1])
		return rt.NilValue, nil
	})
	host("trace_light", []rt.Kind{rt.KVec3, rt.KFloat}, func(a []rt.Value) (rt.Value, error) {
		scene.LightDirection, scene.LightIntensity = vec(a[0]), num(a[1])
		return rt.NilValue, nil
	})
	if err := vm.LoadModule(module); err != nil {
		return Scene{}, err
	}
	if _, err := vm.Call("main"); err != nil {
		return Scene{}, err
	}
	if _, err := Snapshot(scene, Options{Width: 1, Height: 1, Samples: 1}); err != nil {
		return Scene{}, err
	}
	return scene, nil
}
