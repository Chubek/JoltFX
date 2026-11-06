// Package stdlib embeds implemented standard modules. Catalog-only modules are
// deliberately absent and still fail resolution.
package stdlib

import "embed"

//go:embed std/*.ing
var Sources embed.FS
