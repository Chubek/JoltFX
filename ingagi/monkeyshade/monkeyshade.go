package shader

import (
	"context"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

// CompileSource parses a standalone shader compilation unit and lowers the
// selected declaration. This is source compilation, not GPU execution.
func CompileSource(ctx context.Context, source, name, stage, format string, tools ToolOptions) ([]byte, error) {
	s, err := Parse(source, name)
	if err != nil {
		return nil, err
	}
	return CompileArtifact(ctx, s, stage, format, tools)
}

// Declaration is the shared Ingagi shader AST, not a second parser-specific IR.
type Declaration = fe.ShaderDecl
