// Command monkeytrace renders an ECS demo to PNG, compiles its Ingagi shader,
// or exports a self-contained WebGPU browser renderer.
package main

import (
	"context"
	"flag"
	"fmt"
	"image/png"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/Chubek/JoltFX/ingagi/monkeytrace"
	"github.com/Chubek/JoltFX/ingagi/shader"
)

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
func run() error {
	backend := flag.String("backend", "cpu", "cpu, webgpu, or shader")
	source := flag.String("scene", "", "Ingagi scene source file (default: built-in demo)")
	output := flag.String("o", "monkeytrace.png", "output file (use .html for webgpu)")
	format := flag.String("format", "spirv", "shader artifact: glsl, spirv, wgsl, hlsl, msl, glsl-opengl")
	width := flag.Int("width", 640, "image width")
	height := flag.Int("height", 360, "image height")
	samples := flag.Int("samples", 4, "samples per pixel (1..64)")
	bounces := flag.Int("bounces", 3, "reflection bounces (0..8)")
	naga := flag.String("naga", "", "Naga executable (or INGAGI_NAGA)")
	timeout := flag.Duration("timeout", time.Minute, "render/shader build timeout")
	flag.Parse()
	if flag.NArg() != 0 {
		return fmt.Errorf("monkeytrace: unexpected positional arguments")
	}
	if *timeout <= 0 {
		return fmt.Errorf("monkeytrace: timeout must be positive")
	}
	ctx, cancel := context.WithTimeout(context.Background(), *timeout)
	defer cancel()
	options := monkeytrace.Options{Width: *width, Height: *height, Samples: *samples, Bounces: *bounces}
	scene := monkeytrace.DemoScene()
	if *source != "" && *backend == "shader" {
		return fmt.Errorf("monkeytrace: -scene applies to cpu/webgpu rendering")
	}
	if *source != "" && *backend != "shader" {
		input, err := os.Open(*source)
		if err != nil {
			return err
		}
		data, err := io.ReadAll(io.LimitReader(input, 1024*1024+1))
		input.Close()
		if err != nil {
			return err
		}
		if err = ctx.Err(); err != nil {
			return err
		}
		scene, err = monkeytrace.LoadScene(string(data))
		if err != nil {
			return err
		}
		if err = ctx.Err(); err != nil {
			return err
		}
	}
	tools := shader.ToolOptions{Naga: *naga}
	// Publish only successful output; compilation/render errors preserve files.
	write := func(w io.Writer) error {
		switch *backend {
		case "cpu":
			img, err := monkeytrace.Render(ctx, scene, options, monkeytrace.CPUBackend{})
			if err != nil {
				return err
			}
			return png.Encode(w, img)
		case "webgpu":
			return monkeytrace.WriteWebGPU(ctx, w, scene, options, tools)
		case "shader":
			data, err := monkeytrace.ShaderArtifact(ctx, *format, tools)
			if err != nil {
				return err
			}
			_, err = w.Write(data)
			return err
		default:
			return fmt.Errorf("monkeytrace: unknown backend %q", *backend)
		}
	}
	f, err := os.CreateTemp(filepath.Dir(*output), ".monkeytrace-*")
	if err != nil {
		return err
	}
	defer os.Remove(f.Name())
	if err = write(f); err != nil {
		f.Close()
		return err
	}
	if err = f.Close(); err != nil {
		return err
	}
	if err = os.Rename(f.Name(), *output); err != nil {
		return err
	}
	fmt.Printf("MonkeyTrace %s: %s\n", *backend, *output)
	return nil
}
