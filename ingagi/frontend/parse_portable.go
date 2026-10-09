//go:build !novoparse

package frontend

func parseSource(src string) *Result { return NewParser(src).ParseFile() }
