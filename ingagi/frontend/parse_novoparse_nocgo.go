//go:build novoparse && !cgo

package frontend

func parseSource(src string) *Result {
	return &Result{File: &File{}, Errors: []Error{{Msg: "the novoparse build tag requires CGO_ENABLED=1"}}}
}
