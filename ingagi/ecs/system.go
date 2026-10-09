package ecs

import (
	"fmt"
	"sort"
)

// Scheduling: topological order over before/after constraints, phase lists
// for the frame pipeline, and a deferred command buffer applied at phase
// boundaries (so systems never observe half-applied mutations).

// Phase is one frame-pipeline stage, in execution order.
type Phase int

const (
	PhaseStartup Phase = iota
	PhaseUpdate
	PhaseFixedUpdate
	PhaseLateUpdate
	PhasePreRender
	PhasePostRender
	PhaseShutdown
)

// PhaseHookNames maps phases to system hook names.
var PhaseHookNames = map[Phase]string{
	PhaseStartup:     "startup",
	PhaseUpdate:      "update",
	PhaseFixedUpdate: "fixed_update",
	PhaseLateUpdate:  "late_update",
	PhasePreRender:   "pre_render",
	PhasePostRender:  "post_render",
	PhaseShutdown:    "shutdown",
}

// PhaseOrder is the canonical frame order.
var PhaseOrder = []Phase{
	PhaseStartup, PhaseUpdate, PhaseFixedUpdate, PhaseLateUpdate,
	PhasePreRender, PhasePostRender, PhaseShutdown,
}

// SysNode is a schedulable system with ordering constraints.
type SysNode struct {
	Name     string
	Before   []string
	After    []string
	Parallel bool
}

// SortSystems topologically orders systems honoring before/after edges.
// Unknown targets are ignored (cross-module systems); cycles are an error
// listing the participants.
func SortSystems(nodes []SysNode) ([]string, error) {
	known := map[string]bool{}
	for _, n := range nodes {
		known[n.Name] = true
	}
	edges := map[string]map[string]bool{} // a -> {b}: a runs before b
	add := func(a, b string) {
		if !known[a] || !known[b] || a == b {
			return
		}
		if edges[a] == nil {
			edges[a] = map[string]bool{}
		}
		edges[a][b] = true
	}
	for _, n := range nodes {
		for _, b := range n.Before {
			add(n.Name, b)
		}
		for _, a := range n.After {
			add(a, n.Name)
		}
	}
	// Deterministic Kahn's algorithm (lexicographic tie-break).
	indeg := map[string]int{}
	for _, n := range nodes {
		indeg[n.Name] = 0
	}
	for _, tos := range edges {
		for b := range tos {
			indeg[b]++
		}
	}
	var ready []string
	for _, n := range nodes {
		if indeg[n.Name] == 0 {
			ready = append(ready, n.Name)
		}
	}
	var out []string
	for len(ready) > 0 {
		sort.Strings(ready)
		a := ready[0]
		ready = ready[1:]
		out = append(out, a)
		var tos []string
		for b := range edges[a] {
			tos = append(tos, b)
		}
		sort.Strings(tos)
		for _, b := range tos {
			indeg[b]--
			if indeg[b] == 0 {
				ready = append(ready, b)
			}
		}
	}
	if len(out) != len(nodes) {
		var stuck []string
		for _, n := range nodes {
			if indeg[n.Name] > 0 {
				stuck = append(stuck, n.Name)
			}
		}
		sort.Strings(stuck)
		return nil, fmt.Errorf("ingagi: system dependency cycle: %v", stuck)
	}
	return out, nil
}

// Query is a component filter with inclusion, exclusion and optionals.
type Query struct {
	With     []string
	Without  []string
	Optional []string
}

// Match returns member entities (optionals do not filter membership).
func (w *World) Match(q Query) []EntityID { return w.Query(q.With, q.Without) }

// Command is one deferred world mutation.
type Command struct {
	Op     string // spawn|despawn|add|remove|set
	Entity EntityID
	Comp   string
	Fields map[string]any
	Spawn  map[string]map[string]any
	Result *EntityID // spawn writes its ID here on Flush
}

// CommandBuffer records mutations applied atomically at Flush.
type CommandBuffer struct {
	world *World
	cmds  []Command
}

// NewCommandBuffer returns a buffer bound to a world.
func NewCommandBuffer(w *World) *CommandBuffer { return &CommandBuffer{world: w} }

// Spawn queues entity creation.
func (b *CommandBuffer) Spawn(comps map[string]map[string]any, result *EntityID) {
	b.cmds = append(b.cmds, Command{Op: "spawn", Spawn: comps, Result: result})
}

// Despawn queues destruction.
func (b *CommandBuffer) Despawn(e EntityID) {
	b.cmds = append(b.cmds, Command{Op: "despawn", Entity: e})
}

// Add queues component attachment.
func (b *CommandBuffer) Add(e EntityID, comp string, fields map[string]any) {
	b.cmds = append(b.cmds, Command{Op: "add", Entity: e, Comp: comp, Fields: fields})
}

// Remove queues detachment.
func (b *CommandBuffer) Remove(e EntityID, comp string) {
	b.cmds = append(b.cmds, Command{Op: "remove", Entity: e, Comp: comp})
}

// Len reports queued commands.
func (b *CommandBuffer) Len() int { return len(b.cmds) }

// Flush applies queued commands in order and clears the buffer.
func (b *CommandBuffer) Flush() {
	for _, c := range b.cmds {
		switch c.Op {
		case "spawn":
			id := b.world.Spawn(c.Spawn)
			if c.Result != nil {
				*c.Result = id
			}
		case "despawn":
			b.world.Despawn(c.Entity)
		case "add":
			b.world.Add(c.Entity, c.Comp, c.Fields)
		case "remove":
			b.world.Remove(c.Entity, c.Comp)
		}
	}
	b.cmds = b.cmds[:0]
}
