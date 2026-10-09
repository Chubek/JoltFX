package runtime

// Optimize returns a copy of t with unconditional jump chains shortened and
// dead constant-push/pop pairs removed. Every compaction relocates both entry
// points and control-flow operands, including deferred landing pads.
func Optimize(t *Tape) *Tape {
	nt := &Tape{Code: append([]Instr(nil), t.Code...), Pool: t.Pool, Entry: map[string]int{}}
	for name, pc := range t.Entry {
		nt.Entry[name] = pc
	}
	threadJumps(nt.Code)
	for dropPushPop(nt) {
		threadJumps(nt.Code)
	}
	return nt
}

func hasTarget(op Op) bool {
	switch op {
	case OpJump, OpJumpIfFalse, OpJumpIfTrue, OpJumpIfNil, OpDefer, OpForEachNext:
		return true
	}
	return false
}

// Cycles are valid (e.g. an empty infinite loop). Leave their incoming targets
// intact instead of looping during optimization or selecting an arbitrary node.
func threadJumps(code []Instr) {
	original := append([]Instr(nil), code...)
	for i, in := range original {
		if !hasTarget(in.Op) || in.Op == OpDefer {
			continue
		}
		pc := int(in.A)
		seen := map[int]bool{}
		for pc >= 0 && pc < len(original) && original[pc].Op == OpJump {
			if seen[pc] {
				pc = int(in.A)
				break
			}
			seen[pc] = true
			pc = int(original[pc].A)
		}
		code[i].A = int32(pc)
	}
}

func dropPushPop(t *Tape) bool {
	targeted := map[int]bool{}
	for _, pc := range t.Entry {
		targeted[pc] = true
	}
	for _, in := range t.Code {
		if hasTarget(in.Op) {
			targeted[int(in.A)] = true
		}
	}
	removed := make([]bool, len(t.Code))
	changed := false
	for i := 0; i+1 < len(t.Code); i++ {
		if t.Code[i+1].Op != OpPop || targeted[i+1] {
			continue
		}
		switch t.Code[i].Op {
		case OpPushNil, OpPushBool, OpPushInt, OpPushFloat, OpPushString:
			removed[i], removed[i+1] = true, true
			changed = true
			i++
		}
	}
	if !changed {
		return false
	}
	// Include the end-of-tape boundary for forward fixups.
	remap := make([]int, len(t.Code)+1)
	out := make([]Instr, 0, len(t.Code))
	for i, in := range t.Code {
		remap[i] = len(out)
		if !removed[i] {
			out = append(out, in)
		}
	}
	remap[len(t.Code)] = len(out)
	relocate := func(pc int) int {
		if pc >= 0 && pc < len(remap) {
			return remap[pc]
		}
		return pc // invalid targets remain invalid for bytecode validation
	}
	for i := range out {
		if hasTarget(out[i].Op) {
			out[i].A = int32(relocate(int(out[i].A)))
		}
	}
	for name, pc := range t.Entry {
		t.Entry[name] = relocate(pc)
	}
	t.Code = out
	return true
}
