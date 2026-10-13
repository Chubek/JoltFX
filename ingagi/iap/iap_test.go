package iap

import (
	"os"
	"path/filepath"
	"testing"
)

func writeTemp(t *testing.T, blob []byte) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "test.iap")
	if err := os.WriteFile(path, blob, 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestRejectsBadMagic(t *testing.T) {
	if _, err := Decode([]byte("BAD!....too short....")); err == nil {
		t.Fatal("expected error for bad magic")
	}
}

func TestRejectsTruncation(t *testing.T) {
	if _, err := Decode([]byte("IAP1")); err == nil {
		t.Fatal("expected error for truncation")
	}
	if _, err := Load(filepath.Join(t.TempDir(), "missing.iap")); err == nil {
		t.Fatal("expected error for missing file")
	}
}

func TestSummaryMissing(t *testing.T) {
	if _, _, _, _, _, err := Summary(filepath.Join(t.TempDir(), "missing.iap")); err == nil {
		t.Fatal("expected error for missing file")
	}
}

func TestRoundTrip(t *testing.T) {
	objects := []Object{
		{
			Name: "Cube", ClonerMode: 2, Instances: 6, Spacing: 3,
			Keys: []Key{
				{Channel: 4, Frame: 0, Value: 0, Interpolation: 1},
				{Channel: 4, Frame: 90, Value: 360, Interpolation: 1},
			},
		},
		{
			Name: "Plane", ClonerMode: 0, Instances: 1, Spacing: 3,
			Keys: []Key{
				{Channel: 1, Frame: 0, Value: 0, Interpolation: 1},
				{Channel: 1, Frame: 45, Value: 2, Interpolation: 2},
				{Channel: 1, Frame: 90, Value: 0, Interpolation: 1},
			},
		},
	}
	assets := []Asset{{Path: "mesh.ply", Data: []byte("ply-data")}}
	blob, err := Encode(30, 120, objects, assets)
	if err != nil {
		t.Fatal(err)
	}
	path := writeTemp(t, blob)
	got, err := Load(path)
	if err != nil {
		t.Fatal(err)
	}
	if got.FPS != 30 || got.Frames != 120 || len(got.Objects) != 2 || len(got.Assets) != 1 {
		t.Fatalf("unexpected summary: %+v", got)
	}
	if got.Objects[0].Name != "Cube" || len(got.Objects[0].Keys) != 2 ||
		got.Objects[0].Keys[1].Value != 360 {
		t.Fatalf("object 0 mismatch: %+v", got.Objects[0])
	}
	if got.Objects[1].Keys[1].Interpolation != 2 || string(got.Assets[0].Data) != "ply-data" {
		t.Fatalf("object 1/asset mismatch: %+v %+v", got.Objects[1], got.Assets[0])
	}
	fps, frames, nobj, nassets, nbytes, err := Summary(path)
	if err != nil || fps != 30 || frames != 120 || nobj != 2 || nassets != 1 || nbytes != uint64(len(blob)) {
		t.Fatalf("summary mismatch: %d %d %d %d %d %v", fps, frames, nobj, nassets, nbytes, err)
	}
}

func TestRejectsCorruptAndUnordered(t *testing.T) {
	blob, err := Encode(30, 120, []Object{{
		Name: "Cube", ClonerMode: 0, Instances: 1, Spacing: 3,
		Keys: []Key{{Channel: 0, Frame: 0, Value: 1, Interpolation: 1}},
	}}, nil)
	if err != nil {
		t.Fatal(err)
	}
	bad := append([]byte(nil), blob...)
	bad[len(bad)-1] ^= 0xFF
	if _, err := Decode(bad); err == nil {
		t.Fatal("expected CRC error")
	}
	unordered := []Object{{
		Name: "Cube", ClonerMode: 0, Instances: 1, Spacing: 3,
		Keys: []Key{
			{Channel: 1, Frame: 10, Value: 1, Interpolation: 1},
			{Channel: 0, Frame: 90, Value: 2, Interpolation: 1},
		},
	}}
	if _, err := Encode(30, 120, unordered, nil); err == nil {
		t.Fatal("expected ordering error")
	}
	if _, err := Encode(0, 120, nil, nil); err == nil {
		t.Fatal("expected clock error")
	}
	if _, err := Encode(30, 120, []Object{{
		Name: "Bad\x00Name", ClonerMode: 0, Instances: 1, Spacing: 3,
	}}, nil); err == nil {
		t.Fatal("expected name error")
	}
}
