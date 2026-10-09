package frontend

// Tree visitor: structural traversal over File/Decl/Stmt/Expr/Pattern/Type.
// Passes (sema, compilers) implement Visitor and override only the methods
// they need; Walk drives the recursion.

// Visitor receives callbacks in source order. Returning false from any
// method prunes that subtree.
type Visitor interface {
	VisitDecl(d Decl) bool
	VisitStmt(s Stmt) bool
	VisitExpr(e Expr) bool
	VisitPattern(p Pattern) bool
	VisitType(t Type) bool
}

// BaseVisitor prunes nothing; embed it to implement partial visitors.
type BaseVisitor struct{}

func (BaseVisitor) VisitDecl(Decl) bool     { return true }
func (BaseVisitor) VisitStmt(Stmt) bool     { return true }
func (BaseVisitor) VisitExpr(Expr) bool     { return true }
func (BaseVisitor) VisitPattern(Pattern) bool { return true }
func (BaseVisitor) VisitType(Type) bool     { return true }

// Walk traverses f depth-first.
func Walk(v Visitor, f *File) {
	for _, imp := range f.Imports {
		_ = imp
	}
	for _, d := range f.Decls {
		walkDecl(v, d)
	}
}

func walkDecl(v Visitor, d Decl) {
	if !v.VisitDecl(d) {
		return
	}
	switch d := d.(type) {
	case *ConstDecl:
		walkType(v, d.Type)
		walkExpr(v, d.Value)
	case *TypeAliasDecl:
		walkType(v, d.Target)
	case *StructDecl:
		for _, b := range d.Bases {
			walkType(v, b)
		}
		for _, f := range d.Fields {
			walkType(v, f.Type)
		}
		for _, m := range d.Methods {
			walkDecl(v, m)
		}
	case *EnumDecl:
		walkType(v, d.Repr)
		for _, e := range d.Variants {
			for _, t := range e.Types {
				walkType(v, t)
			}
			walkExpr(v, e.Value)
		}
	case *FnDecl:
		for _, a := range d.Args {
			walkType(v, a.Type)
			walkExpr(v, a.Default)
		}
		walkType(v, d.Ret)
		walkBlock(v, d.Body)
	case *ComponentDecl:
		walkType(v, d.Base)
		for _, f := range d.Fields {
			walkType(v, f.Type)
			walkExpr(v, f.Default)
		}
		for _, m := range d.Methods {
			walkDecl(v, m)
		}
	case *EntityDecl:
		for _, a := range d.Attachments {
			for _, in := range a.Inits {
				walkExpr(v, in.Value)
			}
		}
		for _, c := range d.Children {
			walkExpr(v, c.Value)
		}
		for _, pr := range d.Props {
			walkType(v, pr.Type)
			walkExpr(v, pr.Value)
		}
	case *SystemDecl:
		for _, fn := range d.Fns {
			walkDecl(v, fn)
		}
		for _, h := range d.Hooks {
			for _, a := range h.Params {
				walkType(v, a.Type)
			}
			walkBlock(v, h.Body)
		}
	case *EventDecl:
		walkType(v, d.Base)
		for _, f := range d.Fields {
			walkType(v, f.Type)
			walkExpr(v, f.Default)
		}
	case *ResourceDecl:
		walkType(v, d.Type)
		walkExpr(v, d.Value)
	case *ShaderDecl:
		for _, c := range d.Consts {
			walkExpr(v, c.Value)
		}
		for _, s := range d.Specs {
			walkExpr(v, s.Value)
		}
		for _, fn := range d.Funcs {
			walkBlock(v, fn.Body)
		}
		for _, st := range d.Stages {
			for _, o := range st.Options {
				walkExpr(v, o)
			}
			walkBlock(v, st.Body)
		}
		for _, o := range d.Options {
			walkExpr(v, o.Value)
		}
	case *PipelineDecl:
		for _, m := range d.Raster {
			walkExpr(v, m)
		}
		for _, m := range d.Depth {
			walkExpr(v, m)
		}
		for _, m := range d.Blend {
			walkExpr(v, m)
		}
		for _, m := range d.Target {
			walkExpr(v, m)
		}
		for _, m := range d.Compute {
			walkExpr(v, m)
		}
	case *InterfaceDecl:
		for _, m := range d.Methods {
			walkDecl(v, m)
		}
	case *ExtendDecl:
		walkType(v, d.Target)
		walkBlock(v, d.Body)
	case *ExternDecl:
		for _, fn := range d.Funcs {
			walkDecl(v, fn)
		}
	}
}

func walkBlock(v Visitor, b *Block) {
	if b == nil {
		return
	}
	for _, s := range b.Stmts {
		walkStmt(v, s)
	}
}

func walkStmt(v Visitor, s Stmt) {
	if s == nil || !v.VisitStmt(s) {
		return
	}
	switch s := s.(type) {
	case *Block:
		walkBlock(v, s)
	case *VarDeclStmt:
		walkType(v, s.Type)
		walkExpr(v, s.Init)
	case *ExprStmt:
		walkExpr(v, s.X)
	case *ReturnStmt:
		walkExpr(v, s.Value)
	case *BreakStmt:
		walkExpr(v, s.Value)
	case *IfStmt:
		walkExpr(v, s.Cond)
		walkBlock(v, s.Then)
		walkBlock(v, s.Else)
		if s.ElseIf != nil {
			walkStmt(v, s.ElseIf)
		}
	case *WhileStmt:
		walkExpr(v, s.Cond)
		walkBlock(v, s.Body)
	case *ForStmt:
		walkPattern(v, s.Pat)
		walkExpr(v, s.Iter)
		walkBlock(v, s.Body)
	case *LoopStmt:
		walkBlock(v, s.Body)
	case *MatchStmt:
		walkExpr(v, s.X)
		walkArms(v, s.Arms)
	case *DeferStmt:
		walkBlock(v, s.Body)
	case *UnsafeStmt:
		walkBlock(v, s.Body)
	case *ForEachStmt:
		walkExpr(v, s.Iter)
		walkBlock(v, s.Body)
	case *BindStmt:
		walkExpr(v, s.Iter)
	case *EmitStmt:
		walkExpr(v, s.Event)
	case *OnStmt:
		walkBlock(v, s.Body)
	case *DeclStmt:
		walkDecl(v, s.Decl)
	}
}

func walkArms(v Visitor, arms []MatchArm) {
	for _, a := range arms {
		walkPattern(v, a.Pat)
		walkExpr(v, a.Guard)
		walkExpr(v, a.Value)
		walkBlock(v, a.Body)
	}
}

func walkExpr(v Visitor, e Expr) {
	if e == nil || !v.VisitExpr(e) {
		return
	}
	switch e := e.(type) {
	case *UnaryExpr:
		walkExpr(v, e.X)
	case *BinaryExpr:
		walkExpr(v, e.X)
		walkExpr(v, e.Y)
	case *AssignExpr:
		walkExpr(v, e.X)
		walkExpr(v, e.Y)
	case *TernaryExpr:
		walkExpr(v, e.Cond)
		walkExpr(v, e.Then)
		walkExpr(v, e.Else)
	case *CallExpr:
		walkExpr(v, e.Fn)
		for _, a := range e.Args {
			walkExpr(v, a.Value)
		}
	case *MemberExpr:
		walkExpr(v, e.X)
	case *IndexExpr:
		walkExpr(v, e.X)
		walkExpr(v, e.Index)
	case *QuestionExpr:
		walkExpr(v, e.X)
	case *TupleExpr:
		for _, x := range e.Elems {
			walkExpr(v, x)
		}
	case *ArrayExpr:
		for _, x := range e.Elems {
			walkExpr(v, x)
		}
	case *ArrayRepeatExpr:
		walkExpr(v, e.Elem)
		walkExpr(v, e.Count)
	case *StructExpr:
		for _, f := range e.Fields {
			walkExpr(v, f.Value)
		}
	case *LambdaExpr:
		for _, a := range e.Params {
			walkType(v, a.Type)
		}
		walkType(v, e.Ret)
		walkExpr(v, e.Single)
		walkBlock(v, e.Body)
	case *IfExpr:
		walkExpr(v, e.Cond)
		walkBlock(v, e.Then)
		walkBlock(v, e.Else)
		walkExpr(v, e.ElseIf)
	case *MatchExpr:
		walkExpr(v, e.X)
		walkArms(v, e.Arms)
	}
}

func walkPattern(v Visitor, p Pattern) {
	if p == nil || !v.VisitPattern(p) {
		return
	}
	switch p := p.(type) {
	case *TuplePat:
		for _, e := range p.Elems {
			walkPattern(v, e)
		}
	case *StructPat:
		for _, f := range p.Fields {
			walkPattern(v, f.Pat)
		}
	case *ArrayPat:
		for _, e := range p.Elems {
			walkPattern(v, e)
		}
	case *OrPat:
		for _, e := range p.Alts {
			walkPattern(v, e)
		}
	}
}

func walkType(v Visitor, t Type) {
	if t == nil || !v.VisitType(t) {
		return
	}
	switch t := t.(type) {
	case *TypeName:
		for _, a := range t.Args {
			walkType(v, a)
		}
	case *ArrayType:
		walkType(v, t.Elem)
		walkExpr(v, t.Length)
	case *TupleType:
		for _, e := range t.Elems {
			walkType(v, e)
		}
	case *FnType:
		for _, a := range t.Params {
			walkType(v, a)
		}
		walkType(v, t.Ret)
	case *RefType:
		walkType(v, t.Elem)
	case *PtrType:
		walkType(v, t.Elem)
	}
}
