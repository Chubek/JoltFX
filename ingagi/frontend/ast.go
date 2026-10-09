// Package frontend implements Ingagi's scannerless parser, abstract syntax
// tree, and tree visitor.
//
// Grammar authority
//
// The normative grammar is ingagi.ng, written for NovoParse
// (third_party/novoparse), a scannerless GLR parser generator. The Go parser
// in this package is a hand-written deterministic counterpart: every parse
// function maps 1:1 to a rule in ingagi.ng, terminals are matched directly
// against the byte buffer with trivia (whitespace and comments) skipped
// between every token — i.e. there is no separate lexer stage, exactly the
// scannerless model NovoParse uses. Run `novoparse check ingagi.ng` with the
// vendored tool to validate the grammar, and see TestNGRuleCoverage which
// asserts every .ng rule has a matching Go parse function.
//
// Positions are zero-based, end-exclusive byte spans over the original
// source; Line/Col are 1-based for diagnostics.
package frontend

import "fmt"

// ---------------------------------------------------------------------------
// Positions
// ---------------------------------------------------------------------------

// Span is a byte range plus a 1-based starting line/column.
type Span struct {
	Start, End int
	Line, Col  int
}

// Merge returns the union of a and b.
func Merge(a, b Span) Span {
	if a.Start > b.Start {
		a.Start = b.Start
		a.Line, a.Col = b.Line, b.Col
	}
	if a.End < b.End {
		a.End = b.End
	}
	return a
}

func (s Span) String() string { return fmt.Sprintf("%d:%d", s.Line, s.Col) }

// ---------------------------------------------------------------------------
// Attributes, visibility, identifiers
// ---------------------------------------------------------------------------

// Attr is a `@name` or `@name(args...)` annotation. Attributes are the
// extension point for engine concerns without new syntax: @hot (live reload),
// @replicate (netcode), @client/@server (placement), @spec (specialization).
type Attr struct {
	Name string
	Args []Expr
	Span Span
}

// Visibility is "", "public", "private" or "internal".
type Visibility string

const (
	VisDefault  Visibility = ""
	VisPublic   Visibility = "public"
	VisPrivate  Visibility = "private"
	VisInternal Visibility = "internal"
)

// Ident is a resolved identifier occurrence.
type Ident struct {
	Name string
	Span Span
}

// Path is a `(::)a::b::c` qualified name.
type Path struct {
	Global bool
	Parts  []Ident
	Span   Span
}

func (p Path) String() string {
	s := ""
	if p.Global {
		s = "::"
	}
	for i, q := range p.Parts {
		if i > 0 {
			s += "::"
		}
		s += q.Name
	}
	return s
}

// Last returns the final segment, or "".
func (p Path) Last() string {
	if len(p.Parts) == 0 {
		return ""
	}
	return p.Parts[len(p.Parts)-1].Name
}

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// Type is implemented by every type node.
type Type interface{ typ() NodeSpan }

type TypeName struct {
	Path     Path
	Args     []Type // generic arguments
	Optional bool   // trailing `?`
	Span     Span
}
type ArrayType struct {
	Elem   Type
	Length Expr // const expression
	Span   Span
}
type TupleType struct {
	Elems []Type
	Span  Span
}
type FnType struct {
	Params []Type
	Ret    Type // nil => void
	Span   Span
}
type RefType struct {
	Mut  bool
	Elem Type
	Span Span
}
type PtrType struct {
	Mut  bool
	Elem Type
	Span Span
}

func (*TypeName) typ() NodeSpan  { return 0 }
func (*ArrayType) typ() NodeSpan { return 0 }
func (*TupleType) typ() NodeSpan { return 0 }
func (*FnType) typ() NodeSpan    { return 0 }
func (*RefType) typ() NodeSpan   { return 0 }
func (*PtrType) typ() NodeSpan   { return 0 }

// NodeSpan is a zero-size marker so Type/Expr/Stmt/Decl/Pattern are distinct
// interfaces sharing one accessor convention.
type NodeSpan int

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// Expr is implemented by every expression node.
type Expr interface {
	expr()
	Pos() Span
}

type LitKind int

const (
	LitInt LitKind = iota
	LitFloat
	LitBool
	LitString
	LitChar
	LitNull
)

// Lit is a literal.
type Lit struct {
	Kind LitKind
	// Int holds integer value (and char rune); Float holds floats; Str holds
	// unescaped strings; Bool holds booleans.
	Int   int64
	Float float64
	Bool  bool
	Str   string
	Span  Span
}

type IdentExpr struct {
	Name string
	Span Span
}
type PathExpr struct {
	Path Path
	Span Span
}
type SelfExpr struct {
	Span Span
}
type UnaryExpr struct {
	Op   string // ! ~ - + * & await
	X    Expr
	Span Span
}
type BinaryExpr struct {
	Op   string
	X, Y Expr
	Span Span
}
type AssignExpr struct {
	Op   string // = += -= *= /= %= &= |= ^= <<= >>=
	X, Y Expr
	Span Span
}
type TernaryExpr struct {
	Cond, Then, Else Expr
	Span             Span
}
type CallArg struct {
	Name  string // "" for positional
	Value Expr
	Span  Span
}
type CallExpr struct {
	Fn   Expr
	Args []CallArg
	Span Span
}
type MemberExpr struct {
	X     Expr
	Field string
	Arrow bool // -> vs .
	Span  Span
}
type IndexExpr struct {
	X, Index Expr
	Span     Span
}
type QuestionExpr struct {
	X    Expr // postfix `?` (error/option propagate)
	Span Span
}
type TupleExpr struct {
	Elems []Expr
	Span  Span
}
type ArrayExpr struct {
	Elems []Expr
	Span  Span
}
type ArrayRepeatExpr struct {
	Elem, Count Expr
	Span        Span
}
type StructInit struct {
	Field string
	Value Expr // nil => shorthand `Field`
	Span  Span
}
type StructExpr struct {
	Type   Path
	Fields []StructInit
	Span   Span
}
type LambdaExpr struct {
	Params []Param
	Ret    Type
	Body   *Block
	Single Expr // expression body when Body == nil
	Span   Span
}
type IfExpr struct {
	Cond Expr
	Then *Block
	Else *Block
	ElseIf Expr // chained `else if` as expression
	Span Span
}
type MatchArm struct {
	Pat   Pattern
	Guard Expr // nil when absent
	Body  *Block
	Value Expr // expression arm when Body == nil
	Span  Span
}
type MatchExpr struct {
	X    Expr
	Arms []MatchArm
	Span Span
}

func (*Lit) expr()             {}
func (*IdentExpr) expr()       {}
func (*PathExpr) expr()        {}
func (*SelfExpr) expr()        {}
func (*UnaryExpr) expr()       {}
func (*BinaryExpr) expr()      {}
func (*AssignExpr) expr()      {}
func (*TernaryExpr) expr()     {}
func (*CallExpr) expr()        {}
func (*MemberExpr) expr()      {}
func (*IndexExpr) expr()       {}
func (*QuestionExpr) expr()    {}
func (*TupleExpr) expr()       {}
func (*ArrayExpr) expr()       {}
func (*ArrayRepeatExpr) expr() {}
func (*StructExpr) expr()      {}
func (*LambdaExpr) expr()      {}
func (*IfExpr) expr()          {}
func (*MatchExpr) expr()       {}

func (e *Lit) Pos() Span             { return e.Span }
func (e *IdentExpr) Pos() Span       { return e.Span }
func (e *PathExpr) Pos() Span        { return e.Span }
func (e *SelfExpr) Pos() Span        { return e.Span }
func (e *UnaryExpr) Pos() Span       { return e.Span }
func (e *BinaryExpr) Pos() Span      { return e.Span }
func (e *AssignExpr) Pos() Span      { return e.Span }
func (e *TernaryExpr) Pos() Span     { return e.Span }
func (e *CallExpr) Pos() Span        { return e.Span }
func (e *MemberExpr) Pos() Span      { return e.Span }
func (e *IndexExpr) Pos() Span       { return e.Span }
func (e *QuestionExpr) Pos() Span    { return e.Span }
func (e *TupleExpr) Pos() Span       { return e.Span }
func (e *ArrayExpr) Pos() Span       { return e.Span }
func (e *ArrayRepeatExpr) Pos() Span { return e.Span }
func (e *StructExpr) Pos() Span      { return e.Span }
func (e *LambdaExpr) Pos() Span      { return e.Span }
func (e *IfExpr) Pos() Span          { return e.Span }
func (e *MatchExpr) Pos() Span       { return e.Span }

// ---------------------------------------------------------------------------
// Statements
// ---------------------------------------------------------------------------

// Stmt is implemented by every statement node.
type Stmt interface {
	stmt()
	Pos() Span
}

// Param is a function parameter.
type Param struct {
	Attrs   []Attr
	Mut     bool
	Name    string
	Type    Type
	Default Expr
	Span    Span
}

// Block is `{ stmts... }`.
type Block struct {
	Stmts []Stmt
	Span  Span
}

type VarDeclStmt struct {
	Attrs []Attr
	// Kind is "let", "var", or "" (inferred from context).
	Kind string
	Name string
	Type Type // nil when inferred
	Init Expr // nil when uninitialized
	Span Span
}
type ExprStmt struct {
	X    Expr
	Span Span
}
type ReturnStmt struct {
	Value Expr // nil => bare return
	Span  Span
}
type BreakStmt struct {
	Label string
	Value Expr
	Span  Span
}
type ContinueStmt struct {
	Label string
	Span  Span
}
type IfStmt struct {
	Cond Expr
	Then *Block
	Else *Block
	ElseIf Stmt // chained `else if`
	Span Span
}
type WhileStmt struct {
	Cond Expr
	Body *Block
	Span Span
}
type ForStmt struct {
	Pat  Pattern
	Iter Expr
	Body *Block
	Span Span
}
type LoopStmt struct {
	Body *Block
	Span Span
}
type MatchStmt struct {
	X    Expr
	Arms []MatchArm
	Span Span
}
type DeferStmt struct {
	Body *Block
	Span Span
}
type UnsafeStmt struct {
	Body *Block
	Span Span
}

// ForEachStmt is the ECS query loop:
//
//	for each(&mut Position as pos, &Velocity) in world { ... }
//
// Bindings name component types in scope; Iter evaluates to a World/Engine.
type ForEachStmt struct {
	Bindings []QueryBinding
	Iter     Expr
	Body     *Block
	Span     Span
}

// BindStmt attaches a pipeline to entities matching a query — the
// shader<->ECS link that makes shaders first-class in world code:
//
//	bind pipeline SpritePipe to each with (Sprite, Transform) in world;
//	bind shader Lighting:fragment to each with (Material) in world;
type BindStmt struct {
	Pipeline string // pipeline or shader name
	Stage    string // "" (whole pipeline) or stage name
	With    []QueryComp
	Without []QueryComp
	Iter    Expr
	Span    Span
}

// EmitStmt publishes a typed event: `emit Damage{ amount: 10 };`
type EmitStmt struct {
	Event Expr
	Span  Span
}

// DeclStmt wraps a declaration used as a block item (e.g. methods inside
// `extend` bodies). Ingagi permits item declarations in any block; they
// scope to that block.
type DeclStmt struct {
	Decl Decl
	Span Span
}

// OnStmt declares a reactive observer inline:
//
//	on add Health { ... }   on remove Poison { ... }   on set Score { ... }
type OnStmt struct {
	Kind string // add | remove | set
	Comp Path
	Body *Block
	Span Span
}

func (*Block) stmt()        {}
func (*VarDeclStmt) stmt()  {}
func (*ExprStmt) stmt()     {}
func (*ReturnStmt) stmt()   {}
func (*BreakStmt) stmt()    {}
func (*ContinueStmt) stmt() {}
func (*IfStmt) stmt()       {}
func (*WhileStmt) stmt()    {}
func (*ForStmt) stmt()      {}
func (*LoopStmt) stmt()     {}
func (*MatchStmt) stmt()    {}
func (*DeferStmt) stmt()    {}
func (*UnsafeStmt) stmt()   {}
func (*ForEachStmt) stmt()  {}
func (*BindStmt) stmt()     {}
func (*EmitStmt) stmt()     {}
func (*OnStmt) stmt()       {}
func (*DeclStmt) stmt()     {}

func (s *Block) Pos() Span        { return s.Span }
func (s *VarDeclStmt) Pos() Span  { return s.Span }
func (s *ExprStmt) Pos() Span     { return s.Span }
func (s *ReturnStmt) Pos() Span   { return s.Span }
func (s *BreakStmt) Pos() Span    { return s.Span }
func (s *ContinueStmt) Pos() Span { return s.Span }
func (s *IfStmt) Pos() Span       { return s.Span }
func (s *WhileStmt) Pos() Span    { return s.Span }
func (s *ForStmt) Pos() Span      { return s.Span }
func (s *LoopStmt) Pos() Span     { return s.Span }
func (s *MatchStmt) Pos() Span    { return s.Span }
func (s *DeferStmt) Pos() Span    { return s.Span }
func (s *UnsafeStmt) Pos() Span   { return s.Span }
func (s *ForEachStmt) Pos() Span  { return s.Span }
func (s *BindStmt) Pos() Span     { return s.Span }
func (s *EmitStmt) Pos() Span     { return s.Span }
func (s *OnStmt) Pos() Span       { return s.Span }
func (s *DeclStmt) Pos() Span     { return s.Span }

// ---------------------------------------------------------------------------
// Patterns
// ---------------------------------------------------------------------------

// Pattern is implemented by every pattern node.
type Pattern interface {
	pattern()
	Pos() Span
}

type WildcardPat struct{ Span Span }
type BindPat struct {
	Name string
	Span Span
}
type LitPat struct {
	Value *Lit
	Span  Span
}
type PathPat struct {
	Path Path
	Span Span
}
type TuplePat struct {
	Elems []Pattern
	Span  Span
}
type StructFieldPat struct {
	Field string
	Pat   Pattern // nil => shorthand bind
	Span  Span
}
type StructPat struct {
	Type   Path
	Fields []StructFieldPat
	Span   Span
}
type ArrayPat struct {
	Elems []Pattern
	Rest  string // `..rest` name, "" when absent
	HasRest bool
	Span  Span
}
type OrPat struct {
	Alts []Pattern
	Span Span
}

func (*WildcardPat) pattern() {}
func (*BindPat) pattern()     {}
func (*LitPat) pattern()      {}
func (*PathPat) pattern()     {}
func (*TuplePat) pattern()    {}
func (*StructPat) pattern()   {}
func (*ArrayPat) pattern()    {}
func (*OrPat) pattern()       {}

func (p *WildcardPat) Pos() Span { return p.Span }
func (p *BindPat) Pos() Span     { return p.Span }
func (p *LitPat) Pos() Span      { return p.Span }
func (p *PathPat) Pos() Span     { return p.Span }
func (p *TuplePat) Pos() Span    { return p.Span }
func (p *StructPat) Pos() Span   { return p.Span }
func (p *ArrayPat) Pos() Span    { return p.Span }
func (p *OrPat) Pos() Span       { return p.Span }

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

// Decl is implemented by every top-level/member declaration.
type Decl interface {
	decl()
	Pos() Span
	DeclName() string
}

type ImportDecl struct {
	Path  Path
	Alias string
	Span  Span
}

type ConstDecl struct {
	Attrs []Attr
	Vis   Visibility
	Name  string
	Type  Type
	Value Expr
	Span  Span
}
type TypeAliasDecl struct {
	Attrs  []Attr
	Vis    Visibility
	Name   string
	Params []string
	Target Type
	Span   Span
}
type StructField struct {
	Attrs []Attr
	Name  string
	Type  Type
	Span  Span
}
type StructDecl struct {
	Attrs  []Attr
	Vis    Visibility
	Name   string
	Params []string
	Bases  []Type
	Fields []StructField
	Methods []*FnDecl
	Span   Span
}
type EnumVariant struct {
	Name   string
	Types  []Type
	Value  Expr
	Span   Span
}
type EnumDecl struct {
	Attrs    []Attr
	Vis      Visibility
	Name     string
	Repr     Type
	Variants []EnumVariant
	Span     Span
}
type FnDecl struct {
	Attrs  []Attr
	Vis    Visibility
	Async  bool
	Name   string
	Params []string // generics
	Args   []Param
	Ret    Type
	Body   *Block // nil => prototype (`;`)
	Span   Span
}
type ExternDecl struct {
	Lib   string
	Funcs []*FnDecl
	Vars  []*VarDeclStmt
	Span  Span
}
type ExtendDecl struct {
	Target Type
	Body   *Block
	Span   Span
}
type InterfaceDecl struct {
	Attrs   []Attr
	Vis     Visibility
	Name    string
	Methods []*FnDecl
	Vars    []*VarDeclStmt
	Span    Span
}

// ComponentField is a `name: Type [= default];` member.
type ComponentField struct {
	Attrs   []Attr
	Name    string
	Type    Type
	Default Expr
	Span    Span
}
type ComponentDecl struct {
	Attrs   []Attr
	Vis     Visibility
	Name    string
	Params  []string
	Base    Type // `: Type` representation override
	Fields  []ComponentField
	Methods []*FnDecl
	Span    Span
}

// EntityAttachment is `Type { inits... };` inside an entity body.
type EntityAttachment struct {
	Type  Path
	Inits []StructInit
	Span  Span
}
type EntityChild struct {
	Name  string
	Value Expr
	Span  Span
}
type EntityProp struct {
	Name  string
	Type  Type
	Value Expr
	Span  Span
}
type EntityDecl struct {
	Attrs       []Attr
	Vis         Visibility
	Name        string
	Params      []string
	Attachments []EntityAttachment
	Children    []EntityChild
	Props       []EntityProp
	Span        Span
}

// QueryComp is one entry of a with/without/optional list.
type QueryComp struct {
	Mut   bool // &mut
	ByRef bool // & or &mut present at all
	Type  Path
	Alias string
	Span  Span
}

// QueryBinding is one entry of a `for each(...)` list; NameOf binds the
// component type when no explicit type is written.
type QueryBinding struct {
	Mut   bool
	ByRef bool
	Name  string // component type name (or bound name)
	Alias string
	Span  Span
}
type SystemQuery struct {
	Binding string // `query Name with (...) ...;`
	With    []QueryComp
	Without []QueryComp
	Optional []QueryComp
	Span    Span
}
type SystemHook struct {
	Kind   string // startup/shutdown/update/fixed_update/...
	Params []Param
	Body   *Block
	Span   Span
}
type SystemDep struct {
	Kind   string // before | after | parallel
	Target string // qualified name, or "true"/"false" for parallel
	Span   Span
}
type SystemDecl struct {
	Attrs   []Attr
	Vis     Visibility
	Name    string
	Params  []string
	Queries []SystemQuery
	Hooks   []SystemHook
	Deps    []SystemDep
	Fns     []*FnDecl
	Vars    []*VarDeclStmt
	Span    Span
}
type EventDecl struct {
	Attrs  []Attr
	Vis    Visibility
	Name   string
	Params []string
	Base   Type
	Fields []ComponentField
	Span   Span
}
type ResourceDecl struct {
	Attrs []Attr
	Vis   Visibility
	Name  string
	Type  Type
	Value Expr
	Span  Span
}

// ---------------------------------------------------------------------------
// Shaders and pipelines
// ---------------------------------------------------------------------------

// ShaderDecl is a first-class shader: resource bindings, helper functions,
// specialization constants and per-stage bodies in one unit.
type ShaderDecl struct {
	Attrs    []Attr
	Vis      Visibility
	Name     string
	Options  []ShaderOption
	Structs  []ShaderStruct
	Consts   []ShaderConst
	Resources []ShaderResource
	Funcs    []ShaderFunc
	Stages   []ShaderStage
	Specs    []ShaderSpecConst
	Span     Span
}

type ShaderOption struct {
	Name  string
	Value Expr
	Span  Span
}
type ShaderField struct {
	Attrs []Attr
	Name  string
	Type  string // scalar/vector/matrix name or qualified type
	Span  Span
}
type ShaderStruct struct {
	Name   string
	Fields []ShaderField
	Span   Span
}
type ShaderConst struct {
	Name  string
	Type  string
	Value Expr
	Span  Span
}
type ShaderSpecConst struct {
	Name  string
	Type  string
	Value Expr // nil => no default
	Span  Span
}
type ShaderResource struct {
	Kind   string // uniform|storage|texture*|sampler|storage_image
	Access string // read|write|read_write for storage/images
	Name   string
	Type   string
	Fields []ShaderField // buffer block bodies
	Alias  string        // instance name
	Attrs  []Attr
	Span   Span
}
type ShaderParam struct {
	Attrs []Attr
	Name  string
	Type  string
	Span  Span
}
type ShaderFunc struct {
	Name   string
	Params []ShaderParam
	Ret    string
	Body   *Block
	Span   Span
}

// ShaderStage is one `vertex(...) { }` / `fragment { }` / `compute` / ...
// block. The body uses the shared statement/expression grammar so host and
// GPU code share one language surface.
type ShaderStage struct {
	Kind    string // vertex|fragment|compute|geometry|tess_control|...
	Options map[string]Expr
	Body    *Block
	Span    Span
}

// PipelineDecl wires shader stages plus fixed-function state into a
// backend-agnostic descriptor the engine tab can instantiate directly.
type PipelineDecl struct {
	Attrs  []Attr
	Vis    Visibility
	Name   string
	Stages map[string]Path // vertex: Shader::stage ...
	Layout Path
	VertexLayout Path
	Raster map[string]Expr
	Depth  map[string]Expr
	Blend  map[string]Expr
	Target map[string]Expr
	Compute map[string]Expr
	Span   Span
}

func (*ImportDecl) decl()    {}
func (*ConstDecl) decl()     {}
func (*TypeAliasDecl) decl() {}
func (*StructDecl) decl()    {}
func (*EnumDecl) decl()      {}
func (*FnDecl) decl()        {}
func (*ExternDecl) decl()    {}
func (*ExtendDecl) decl()    {}
func (*InterfaceDecl) decl() {}
func (*ComponentDecl) decl() {}
func (*EntityDecl) decl()    {}
func (*SystemDecl) decl()    {}
func (*EventDecl) decl()     {}
func (*ResourceDecl) decl()  {}
func (*ShaderDecl) decl()    {}
func (*PipelineDecl) decl()  {}

func (d *ImportDecl) Pos() Span    { return d.Span }
func (d *ConstDecl) Pos() Span     { return d.Span }
func (d *TypeAliasDecl) Pos() Span { return d.Span }
func (d *StructDecl) Pos() Span    { return d.Span }
func (d *EnumDecl) Pos() Span      { return d.Span }
func (d *FnDecl) Pos() Span        { return d.Span }
func (d *ExternDecl) Pos() Span    { return d.Span }
func (d *ExtendDecl) Pos() Span    { return d.Span }
func (d *InterfaceDecl) Pos() Span { return d.Span }
func (d *ComponentDecl) Pos() Span { return d.Span }
func (d *EntityDecl) Pos() Span    { return d.Span }
func (d *SystemDecl) Pos() Span    { return d.Span }
func (d *EventDecl) Pos() Span     { return d.Span }
func (d *ResourceDecl) Pos() Span  { return d.Span }
func (d *ShaderDecl) Pos() Span    { return d.Span }
func (d *PipelineDecl) Pos() Span  { return d.Span }

func (d *ImportDecl) DeclName() string    { return d.Path.Last() }
func (d *ConstDecl) DeclName() string     { return d.Name }
func (d *TypeAliasDecl) DeclName() string { return d.Name }
func (d *StructDecl) DeclName() string    { return d.Name }
func (d *EnumDecl) DeclName() string      { return d.Name }
func (d *FnDecl) DeclName() string        { return d.Name }
func (d *ExternDecl) DeclName() string    { return "extern" }
func (d *ExtendDecl) DeclName() string    { return "extend" }
func (d *InterfaceDecl) DeclName() string { return d.Name }
func (d *ComponentDecl) DeclName() string { return d.Name }
func (d *EntityDecl) DeclName() string    { return d.Name }
func (d *SystemDecl) DeclName() string    { return d.Name }
func (d *EventDecl) DeclName() string     { return d.Name }
func (d *ResourceDecl) DeclName() string  { return d.Name }
func (d *ShaderDecl) DeclName() string    { return d.Name }
func (d *PipelineDecl) DeclName() string  { return d.Name }

// File is a parsed compilation unit.
type File struct {
	Module  Path
	Imports []ImportDecl
	Decls   []Decl
	Span    Span
}
