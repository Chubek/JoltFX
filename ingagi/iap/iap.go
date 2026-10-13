// Package iap reads and inspects Ingagi Animation Pack (.iap) files.
// The writer lives in JoltFX Core (src/src/iap.cpp, jfx_iap.h v1); this
// package mirrors the layout for tooling and the future player. No
// execution, no GPU: pure parsing with the same bounds as Core.
package iap

import (
	"encoding/binary"
	"errors"
	"hash/crc32"
	"math"
	"os"
)

// Limits mirror jfx_iap.h.
const (
	Magic         = "IAP1"
	Version       = 1
	MaxObjects    = 64
	MaxKeys       = 4096
	MaxAssets     = 16
	MaxAssetBytes = 16 * 1024 * 1024
	MaxTotalBytes = 64 * 1024 * 1024
)

// Key is one animation event: channel/frame/value/interpolation.
type Key struct {
	Channel       uint32
	Frame         uint32
	Value         float32
	Interpolation uint32
}

// Object is one packed object's event stream plus cloner state.
type Object struct {
	Name       string
	ClonerMode uint32
	Instances  uint32
	Spacing    float32
	Keys       []Key
}

// Asset is one packed file entry (path + bytes).
type Asset struct {
	Path string
	Data []byte
}

// Pack is a decoded .iap.
type Pack struct {
	FPS     uint32
	Frames  uint32
	Objects []Object
	Assets  []Asset
}

func readU32(b []byte, at *int) (uint32, error) {
	if *at+4 > len(b) {
		return 0, errors.New("iap: truncated u32")
	}
	v := binary.LittleEndian.Uint32(b[*at:])
	*at += 4
	return v, nil
}

func readU64(b []byte, at *int) (uint64, error) {
	if *at+8 > len(b) {
		return 0, errors.New("iap: truncated u64")
	}
	v := binary.LittleEndian.Uint64(b[*at:])
	*at += 8
	return v, nil
}

func readF32(b []byte, at *int) (float32, error) {
	u, err := readU32(b, at)
	if err != nil {
		return 0, err
	}
	return math.Float32frombits(u), nil
}

// Decode parses a .iap blob with full bounds/ordering/CRC checks.
func Decode(blob []byte) (*Pack, error) {
	if len(blob) < 20 {
		return nil, errors.New("iap: too short")
	}
	if string(blob[:4]) != Magic {
		return nil, errors.New("iap: bad magic")
	}
	at := 4
	version, err := readU32(blob, &at)
	if err != nil || version != Version {
		return nil, errors.New("iap: unsupported version")
	}
	fps, err := readU32(blob, &at)
	if err != nil || fps < 1 || fps > 240 {
		return nil, errors.New("iap: bad fps")
	}
	frames, err := readU32(blob, &at)
	if err != nil || frames < 1 || frames > 36000 {
		return nil, errors.New("iap: bad frames")
	}
	objects, err := readU32(blob, &at)
	if err != nil || objects > MaxObjects {
		return nil, errors.New("iap: bad object count")
	}
	p := &Pack{FPS: fps, Frames: frames}
	for o := uint32(0); o < objects; o++ {
		if at+1 > len(blob) {
			return nil, errors.New("iap: truncated name")
		}
		n := int(blob[at])
		at++
		if n > 127 || at+n > len(blob) {
			return nil, errors.New("iap: bad name")
		}
		name := string(blob[at : at+n])
		for i := 0; i < n; i++ {
			if blob[at+i] < 32 || blob[at+i] == 0 {
				return nil, errors.New("iap: bad name bytes")
			}
		}
		at += n
		mode, err := readU32(blob, &at)
		if err != nil || mode > 3 {
			return nil, errors.New("iap: bad cloner mode")
		}
		instances, err := readU32(blob, &at)
		if err != nil || instances < 1 || instances > 64 || ((mode == 0) != (instances == 1)) {
			return nil, errors.New("iap: bad instances")
		}
		spacing, err := readF32(blob, &at)
		if err != nil || spacing < 0.01 || spacing > 1000 {
			return nil, errors.New("iap: bad spacing")
		}
		nkeys, err := readU32(blob, &at)
		if err != nil || nkeys > MaxKeys {
			return nil, errors.New("iap: bad key count")
		}
		obj := Object{Name: name, ClonerMode: mode, Instances: instances, Spacing: spacing}
		var last uint64
		first := true
		for k := uint32(0); k < nkeys; k++ {
			ch, err := readU32(blob, &at)
			if err != nil || ch > 8 {
				return nil, errors.New("iap: bad channel")
			}
			fr, err := readU32(blob, &at)
			if err != nil || fr >= frames {
				return nil, errors.New("iap: bad frame")
			}
			v, err := readF32(blob, &at)
			if err != nil {
				return nil, errors.New("iap: bad value")
			}
			if math.IsNaN(float64(v)) || math.IsInf(float64(v), 0) {
				return nil, errors.New("iap: bad value")
			}
			it, err := readU32(blob, &at)
			if err != nil || it > 2 {
				return nil, errors.New("iap: bad interpolation")
			}
			order := uint64(ch)*36000 + uint64(fr)
			if !first && order <= last {
				return nil, errors.New("iap: keys out of order")
			}
			last, first = order, false
			obj.Keys = append(obj.Keys, Key{Channel: ch, Frame: fr, Value: v, Interpolation: it})
		}
		p.Objects = append(p.Objects, obj)
	}
	nassets, err := readU32(blob, &at)
	if err != nil || nassets > MaxAssets {
		return nil, errors.New("iap: bad asset count")
	}
	var total uint64
	for a := uint32(0); a < nassets; a++ {
		if at+1 > len(blob) {
			return nil, errors.New("iap: truncated asset")
		}
		pl := int(blob[at])
		at++
		if pl < 1 || pl > 127 || at+pl > len(blob) {
			return nil, errors.New("iap: bad asset path")
		}
		path := string(blob[at : at+pl])
		for i := 0; i < pl; i++ {
			if blob[at+i] == 0 {
				return nil, errors.New("iap: bad asset path bytes")
			}
		}
		at += pl
		size, err := readU64(blob, &at)
		if err != nil || size > MaxAssetBytes {
			return nil, errors.New("iap: bad asset size")
		}
		total += size
		if total > MaxTotalBytes {
			return nil, errors.New("iap: pack too large")
		}
		if uint64(len(blob)-at) < size {
			return nil, errors.New("iap: truncated asset bytes")
		}
		data := append([]byte(nil), blob[at:at+int(size)]...)
		at += int(size)
		p.Assets = append(p.Assets, Asset{Path: path, Data: data})
	}
	if at+4 != len(blob) {
		return nil, errors.New("iap: trailing bytes")
	}
	want := binary.LittleEndian.Uint32(blob[at:])
	got := crc32.ChecksumIEEE(blob[:len(blob)-4])
	if want != got {
		return nil, errors.New("iap: crc mismatch")
	}
	return p, nil
}

// Load reads and decodes a .iap file.
func Load(path string) (*Pack, error) {
	blob, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	if uint64(len(blob)) > MaxTotalBytes+1024*1024 {
		return nil, errors.New("iap: file too large")
	}
	return Decode(blob)
}

// Encode serializes fps/frames/objects/assets into a .iap blob, applying
// the same bounds, key-ordering and CRC rules as the Core writer so Go
// tooling can produce packs the C++ inspector accepts (and vice versa).
func Encode(fps, frames uint32, objects []Object, assets []Asset) ([]byte, error) {
	if fps < 1 || fps > 240 || frames < 1 || frames > 36000 {
		return nil, errors.New("iap: bad clock")
	}
	if len(objects) > MaxObjects {
		return nil, errors.New("iap: bad object count")
	}
	if len(assets) > MaxAssets {
		return nil, errors.New("iap: bad asset count")
	}
	out := []byte(Magic)
	putU32 := func(v uint32) {
		var b [4]byte
		binary.LittleEndian.PutUint32(b[:], v)
		out = append(out, b[:]...)
	}
	putU32(Version)
	putU32(fps)
	putU32(frames)
	putU32(uint32(len(objects)))
	for _, o := range objects {
		if len(o.Name) > 127 {
			return nil, errors.New("iap: bad name")
		}
		for i := 0; i < len(o.Name); i++ {
			if o.Name[i] < 32 || o.Name[i] == 0 {
				return nil, errors.New("iap: bad name bytes")
			}
		}
		if o.ClonerMode > 3 || o.Instances < 1 || o.Instances > 64 ||
			((o.ClonerMode == 0) != (o.Instances == 1)) {
			return nil, errors.New("iap: bad cloner")
		}
		if math.IsNaN(float64(o.Spacing)) || math.IsInf(float64(o.Spacing), 0) ||
			o.Spacing < 0.01 || o.Spacing > 1000 {
			return nil, errors.New("iap: bad spacing")
		}
		if len(o.Keys) > MaxKeys {
			return nil, errors.New("iap: bad key count")
		}
		out = append(out, byte(len(o.Name)))
		out = append(out, o.Name...)
		putU32(o.ClonerMode)
		putU32(o.Instances)
		var sb [4]byte
		binary.LittleEndian.PutUint32(sb[:], math.Float32bits(o.Spacing))
		out = append(out, sb[:]...)
		putU32(uint32(len(o.Keys)))
		var last uint64
		first := true
		for _, k := range o.Keys {
			if k.Channel > 8 || k.Frame >= frames || k.Interpolation > 2 {
				return nil, errors.New("iap: bad key")
			}
			if math.IsNaN(float64(k.Value)) || math.IsInf(float64(k.Value), 0) {
				return nil, errors.New("iap: bad value")
			}
			order := uint64(k.Channel)*36000 + uint64(k.Frame)
			if !first && order <= last {
				return nil, errors.New("iap: keys out of order")
			}
			last, first = order, false
			putU32(k.Channel)
			putU32(k.Frame)
			var vb [4]byte
			binary.LittleEndian.PutUint32(vb[:], math.Float32bits(k.Value))
			out = append(out, vb[:]...)
			putU32(k.Interpolation)
		}
	}
	putU32(uint32(len(assets)))
	var total uint64
	for _, a := range assets {
		if len(a.Path) < 1 || len(a.Path) > 127 {
			return nil, errors.New("iap: bad asset path")
		}
		for i := 0; i < len(a.Path); i++ {
			if a.Path[i] == 0 {
				return nil, errors.New("iap: bad asset path bytes")
			}
		}
		if len(a.Data) > MaxAssetBytes {
			return nil, errors.New("iap: bad asset size")
		}
		total += uint64(len(a.Data))
		if total > MaxTotalBytes {
			return nil, errors.New("iap: pack too large")
		}
		out = append(out, byte(len(a.Path)))
		out = append(out, a.Path...)
		var lb [8]byte
		binary.LittleEndian.PutUint64(lb[:], uint64(len(a.Data)))
		out = append(out, lb[:]...)
		out = append(out, a.Data...)
	}
	var cb [4]byte
	binary.LittleEndian.PutUint32(cb[:], crc32.ChecksumIEEE(out))
	out = append(out, cb[:]...)
	return out, nil
}

// Summary returns fps/frames/objects/assets/bytes without exposing blobs.
func Summary(path string) (fps, frames, objects, assets uint32, bytes uint64, err error) {
	blob, readErr := os.ReadFile(path)
	if readErr != nil {
		return 0, 0, 0, 0, 0, readErr
	}
	p, decErr := Decode(blob)
	if decErr != nil {
		return 0, 0, 0, 0, 0, decErr
	}
	return p.FPS, p.Frames, uint32(len(p.Objects)), uint32(len(p.Assets)), uint64(len(blob)), nil
}
