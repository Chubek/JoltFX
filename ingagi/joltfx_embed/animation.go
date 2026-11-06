//go:build cgo

package main

/*
#define INGAGI_ANIMATION_EXPORTS
#include "../animation/native.h"
static int animation_output(ig_animation_output fn, void *user, const char *data, size_t size) {
 return fn(user,data,size);
}
*/
import "C"

import (
	"fmt"
	"github.com/Chubek/JoltFX/ingagi/animation"
	"github.com/Chubek/JoltFX/ingagi/animation/author"
	"unsafe"
)

func animationOutput(d animation.Document, target C.int, output C.ig_animation_output, user unsafe.Pointer) error {
	var s string
	var err error
	switch target {
	case 0:
		s, err = d.Drawing()
	case 1:
		s, err = d.HTML()
	case 2:
		s, err = d.SVG(0)
	default:
		return fmt.Errorf("animation: unknown export target")
	}
	if err != nil {
		return err
	}
	b := []byte(s)
	if C.animation_output(output, user, (*C.char)(unsafe.Pointer(&b[0])), C.size_t(len(b))) != 0 {
		return fmt.Errorf("animation: output publication failed")
	}
	return nil
}
func animationResult(err error, dst *C.char, capacity C.size_t) C.int {
	if err != nil {
		message(dst, capacity, err.Error())
		return 1
	}
	message(dst, capacity, "")
	return 0
}
func animationRecover(result *C.int, dst *C.char, capacity C.size_t) {
	if p := recover(); p != nil {
		*result = animationResult(fmt.Errorf("animation: %v", p), dst, capacity)
	}
}

//export ig_animation_bake
func ig_animation_bake(source *C.char, size C.size_t, fps, frames, target C.int, output C.ig_animation_output, user unsafe.Pointer, diag *C.char, capacity C.size_t) (result C.int) {
	defer animationRecover(&result, diag, capacity)
	if output == nil || target < 0 || target > 2 {
		return animationResult(fmt.Errorf("animation: missing output or invalid target"), diag, capacity)
	}
	src, err := text(source, size, 16384)
	if err != nil {
		return animationResult(err, diag, capacity)
	}
	d, err := author.Bake(src, int(fps), int(frames))
	if err == nil {
		err = animationOutput(d, target, output, user)
	}
	return animationResult(err, diag, capacity)
}

//export ig_animation_export
func ig_animation_export(source *C.char, size C.size_t, target C.int, output C.ig_animation_output, user unsafe.Pointer, diag *C.char, capacity C.size_t) (result C.int) {
	defer animationRecover(&result, diag, capacity)
	if output == nil || target < 0 || target > 2 {
		return animationResult(fmt.Errorf("animation: missing output or invalid target"), diag, capacity)
	}
	src, err := text(source, size, 64<<20)
	if err != nil {
		return animationResult(err, diag, capacity)
	}
	d, err := animation.ReadDrawing(src)
	if err == nil {
		err = animationOutput(d, target, output, user)
	}
	return animationResult(err, diag, capacity)
}
