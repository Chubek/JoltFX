// Package author compiles Ingagi frame(t) functions and bakes renderer-neutral
// vector cels. Publication is atomic: errors never return a partial document.
package author

import (
	"fmt"
	"io"
	"io/fs"

	"github.com/Chubek/JoltFX/ingagi"
	"github.com/Chubek/JoltFX/ingagi/animation"
	rt "github.com/Chubek/JoltFX/ingagi/runtime"
)

func Bake(source string, fps, frames int) (animation.Document, error) {
	d := animation.Document{FPS: fps}
	if fps < 1 || fps > 60 || frames < 1 || frames > animation.MaxFrames || len(source) > 16384 {
		return d, fmt.Errorf("animation: source/timeline limit exceeded")
	}
	module, err := ingagi.CompileFile(sourceFile(source), "animation.ing")
	if err != nil {
		return animation.Document{}, err
	}
	vm := rt.New()
	vm.SetStdout(io.Discard)
	vm.SetBudget(rt.Budget{MaxInstrs: 100000, MaxFrames: 64})
	if err = vm.LoadModule(module); err != nil {
		return animation.Document{}, err
	}
	d.Frames = make([][]animation.Shape, frames)
	total := 0
	for i := range d.Frames {
		v, err := vm.Call("frame", rt.FloatValue(float64(i)/float64(fps)))
		if err != nil {
			return animation.Document{}, fmt.Errorf("frame %d: %w", i+1, err)
		}
		d.Frames[i], err = rt.AnimationFrame(v)
		if err != nil {
			return animation.Document{}, fmt.Errorf("frame %d: %w", i+1, err)
		}
		for _, s := range d.Frames[i] {
			total += len(s.Points)
		}
		if total > animation.MaxTotalPoints {
			return animation.Document{}, fmt.Errorf("animation: total point budget exceeded")
		}
	}
	return d, nil
}

// Only the supplied source and bundled standard modules are accessible.
type sourceFile string

func (s sourceFile) Open(name string) (fs.File, error) { return nil, fs.ErrNotExist }
func (s sourceFile) ReadFile(name string) ([]byte, error) {
	if name != "animation.ing" {
		return nil, fs.ErrNotExist
	}
	return []byte(s), nil
}
