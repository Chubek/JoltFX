package ecs

import "testing"

func TestSpawnQuery(t *testing.T) {
	w := NewWorld()
	a := w.Spawn(map[string]map[string]any{"Pos": {"x": 1}})
	b := w.Spawn(map[string]map[string]any{"Pos": {"x": 2}, "Vel": {}})
	if w.Count() != 2 {
		t.Fatalf("count %d", w.Count())
	}
	got := w.Query([]string{"Pos", "Vel"}, nil)
	if len(got) != 1 || got[0] != b {
		t.Fatalf("query: %v", got)
	}
	_ = a
	if f, ok := w.Get(b, "Pos"); !ok || f["x"] != 2 {
		t.Fatalf("get: %v", f)
	}
	w.Remove(b, "Vel")
	if len(w.Query([]string{"Pos", "Vel"}, nil)) != 0 {
		t.Fatal("remove failed")
	}
	w.Despawn(a)
	if w.Alive(a) || w.Count() != 1 {
		t.Fatal("despawn failed")
	}
	// Slot reuse bumps the generation: old handle stays dead.
	c := w.Spawn(nil)
	if c.Index != a.Index || c.Gen == a.Gen {
		t.Fatalf("reuse: %+v vs %+v", c, a)
	}
	if w.Alive(a) {
		t.Fatal("stale handle alive")
	}
}

func TestWithout(t *testing.T) {
	w := NewWorld()
	w.Spawn(map[string]map[string]any{"A": {}})
	w.Spawn(map[string]map[string]any{"A": {}, "B": {}})
	got := w.Query([]string{"A"}, []string{"B"})
	if len(got) != 1 {
		t.Fatalf("without: %v", got)
	}
}

func TestSortSystems(t *testing.T) {
	order, err := SortSystems([]SysNode{
		{Name: "Render", After: []string{"Physics"}},
		{Name: "Physics", After: []string{"Input"}},
		{Name: "Input"},
	})
	if err != nil {
		t.Fatal(err)
	}
	want := []string{"Input", "Physics", "Render"}
	for i := range want {
		if order[i] != want[i] {
			t.Fatalf("order %v", order)
		}
	}
	if _, err := SortSystems([]SysNode{
		{Name: "A", After: []string{"B"}},
		{Name: "B", After: []string{"A"}},
	}); err == nil {
		t.Fatal("expected cycle error")
	}
}

func TestCommandBuffer(t *testing.T) {
	w := NewWorld()
	cb := NewCommandBuffer(w)
	var id EntityID
	cb.Spawn(map[string]map[string]any{"H": {"hp": 3}}, &id)
	if w.Count() != 0 {
		t.Fatal("buffer must defer")
	}
	cb.Flush()
	if w.Count() != 1 || !w.Alive(id) {
		t.Fatal("flush failed")
	}
	cb.Add(id, "P", map[string]any{"x": 1})
	cb.Flush()
	if !w.Has(id, "P") {
		t.Fatal("add failed")
	}
}

func TestHierarchy(t *testing.T) {
	w := NewWorld()
	p := w.Spawn(nil)
	c := w.Spawn(nil)
	w.SetParent(c, p)
	if got, ok := w.GetParent(c); !ok || got != p {
		t.Fatal("parent edge")
	}
	if len(w.Children(p)) != 1 {
		t.Fatal("children edge")
	}
	w.Despawn(p)
	if _, ok := w.GetParent(c); ok {
		t.Fatal("edge not released")
	}
}

func TestResources(t *testing.T) {
	w := NewWorld()
	w.SetResource("dt", 0.016)
	v, ok := w.GetResource("dt")
	if !ok || v != 0.016 {
		t.Fatalf("resource: %v", v)
	}
}
