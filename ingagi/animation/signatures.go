package animation

// Signatures is shared by VM registration and gradual semantic checks. The map
// and array representations remain ordinary owned Ingagi values.
type Signature struct {
	Result string
	Args   []string
}

var Signatures = map[string]Signature{
	"anim_rgba":      {"int", []string{"float", "float", "float", "float"}},
	"anim_ease":      {"float", []string{"string", "float"}},
	"anim_tween":     {"float", []string{"float", "float", "float", "string"}},
	"anim_track":     {"float", []string{"[float]", "[float]", "float", "string"}},
	"anim_loop":      {"float", []string{"float", "float"}},
	"anim_pingpong":  {"float", []string{"float", "float"}},
	"anim_bezier":    {"vec2", []string{"[vec2]", "float"}},
	"anim_curve":     {"[vec2]", []string{"[vec2]", "int"}},
	"anim_length":    {"float", []string{"[vec2]"}},
	"anim_along":     {"vec2", []string{"[vec2]", "float"}},
	"anim_morph":     {"[vec2]", []string{"[vec2]", "[vec2]", "float"}},
	"anim_transform": {"[vec2]", []string{"[vec2]", "[float]"}},
	"anim_translate": {"[vec2]", []string{"[vec2]", "vec2"}},
	"anim_rotate":    {"[vec2]", []string{"[vec2]", "float", "vec2"}},
	"anim_scale":     {"[vec2]", []string{"[vec2]", "vec2", "vec2"}},
	"anim_arc":       {"[vec2]", []string{"vec2", "vec2", "float", "float", "int"}},
	"anim_star":      {"[vec2]", []string{"vec2", "float", "float", "int", "float"}},
	"anim_rect":      {"?", []string{"vec2", "vec2", "int", "int", "float"}},
	"anim_ellipse":   {"?", []string{"vec2", "vec2", "int", "int", "float"}},
	"anim_line":      {"?", []string{"vec2", "vec2", "int", "float"}},
	"anim_path":      {"?", []string{"[vec2]", "bool", "int", "int", "float"}},
	"anim_opacity":   {"?", []string{"?", "float"}},
	"anim_layer":     {"[?]", []string{"[?]", "int"}},
}
