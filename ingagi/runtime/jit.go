package runtime

import "sort"

// Specializing tier (jit.go): profile-guided fast paths, not machine code.
//
// Full machine-code JITs (SLJIT/QBE backends in the wider JoltFX tree) are
// out of scope for the embeddable scripting VM; instead the VM specializes
// hot instruction sites by observed type: monomorphic arithmetic skips
// kind checks, and method call sites cache receiver-type resolution. The
// policy is tiny and predictable — appropriate for frame-budgeted game
// code — and HotReport exposes it to profilers.

// Kind bits observed by the profiler.
const (
	profInt = 1 << iota
	profFloat
	profOther
)

// hotThreshold is executions before specialization kicks in.
const hotThreshold = 255

// specialized reports whether the site at pc is hot and monomorphic, and
// if so which single kind it saw.
func (vm *VM) specialized(pc int) (bool, int) {
	pr, ok := vm.profiles[pc]
	if !ok || pr.count < hotThreshold {
		return false, 0
	}
	switch pr.seen {
	case profInt:
		return true, profInt
	case profFloat:
		return true, profFloat
	}
	return false, 0
}

// methodCache looks up a monomorphic method resolution for pc.
func (vm *VM) methodCache(pc int, recv string) (string, bool) {
	cc, ok := vm.caches[pc]
	if !ok {
		cc = &inlineCache{}
		vm.caches[pc] = cc
	}
	if cc.key == recv && cc.kind == 1 {
		cc.hit++
		return vm.methods[recv+"::"+cc.method], true
	}
	return "", false
}

// methodCacheFill records a resolution. Mismatched receivers demote the
// site to megamorphic (kind 2, no caching).
func (vm *VM) methodCacheFill(pc int, recv, method, entry string) {
	cc, ok := vm.caches[pc]
	if !ok {
		cc = &inlineCache{}
		vm.caches[pc] = cc
	}
	if cc.kind == 1 && cc.key != recv {
		cc.kind = 2 // megamorphic
		return
	}
	cc.kind, cc.key, cc.method, cc.entry = 1, recv, method, entry
}

// HotSpot is one profiled site for tooling.
type HotSpot struct {
	PC    int
	Count int
	Kind  string
	Hits  int
}

// HotReport returns profiled sites sorted by execution count.
func (vm *VM) HotReport(n int) []HotSpot {
	var out []HotSpot
	for pc, pr := range vm.profiles {
		kind := "poly"
		switch pr.seen {
		case profInt:
			kind = "int"
		case profFloat:
			kind = "float"
		}
		hits := 0
		if cc, ok := vm.caches[pc]; ok {
			hits = cc.hit
		}
		out = append(out, HotSpot{PC: pc, Count: pr.count, Kind: kind, Hits: hits})
	}
	sort.Slice(out, func(i, j int) bool { return out[i].Count > out[j].Count })
	if n > 0 && len(out) > n {
		out = out[:n]
	}
	return out
}
