package main

import (
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"

	"github.com/Chubek/JoltFX/ingagi/animation/author"
)

func animationCLI(args []string, stderr io.Writer) int {
	flags := flag.NewFlagSet("ingagi animation", flag.ContinueOnError)
	flags.SetOutput(stderr)
	output := flags.String("o", "", "output path")
	target := flags.String("target", "canvas", "canvas, svg or drawing")
	fps := flags.Int("fps", 24, "frames per second (1..60)")
	frames := flags.Int("frames", 120, "frame count (1..240)")
	frame := flags.Int("frame", 1, "SVG frame, 1-based")
	if err := flags.Parse(args); err != nil {
		return 2
	}
	if flags.NArg() != 1 || *output == "" {
		fmt.Fprintln(stderr, "usage: ingagi animation -target canvas|svg|drawing -o OUTPUT [-fps 24 -frames 120 -frame 1] FILE.ing")
		return 2
	}
	if *target != "canvas" && *target != "svg" && *target != "drawing" {
		fmt.Fprintln(stderr, "unknown animation target")
		return 2
	}
	input := flags.Arg(0)
	a, _ := filepath.Abs(input)
	b, _ := filepath.Abs(*output)
	inInfo, inErr := os.Stat(input)
	outInfo, outErr := os.Stat(*output)
	if a == b || inErr == nil && outErr == nil && os.SameFile(inInfo, outInfo) {
		fmt.Fprintln(stderr, "output must not replace source")
		return 2
	}
	fail := func(err error) int { fmt.Fprintln(stderr, err); return 1 }
	f, err := os.Open(input)
	if err != nil {
		return fail(err)
	}
	src, err := io.ReadAll(io.LimitReader(f, 16385))
	f.Close()
	if err != nil {
		return fail(err)
	}
	d, err := author.Bake(string(src), *fps, *frames)
	if err != nil {
		return fail(err)
	}
	var result string
	switch *target {
	case "canvas":
		result, err = d.HTML()
	case "svg":
		result, err = d.SVG(*frame - 1)
	case "drawing":
		result, err = d.Drawing()
	}
	if err != nil {
		return fail(err)
	}
	if err = writeArtifact(*output, []byte(result)); err != nil {
		return fail(err)
	}
	return 0
}
