package noise

import (
	"encoding/json"
	"fmt"
	"math"
)

// Profile describes normalized fractional Brownian motion. Octaves are 1..32,
// frequency (0,1e6], lacunarity [1,8] and gain [0,1].
type Profile struct {
	Seed       uint64  `json:"seed"`
	Octaves    int     `json:"octaves"`
	Frequency  float64 `json:"frequency"`
	Lacunarity float64 `json:"lacunarity"`
	Gain       float64 `json:"gain"`
}

func DefaultProfile(seed uint64) Profile { return Profile{seed, 4, 1, 2, .5} }
func (p Profile) Validate() error {
	finite := func(v float64) bool { return !math.IsNaN(v) && !math.IsInf(v, 0) }
	if p.Octaves < 1 || p.Octaves > 32 || !finite(p.Frequency) || p.Frequency <= 0 || p.Frequency > 1e6 ||
		!finite(p.Lacunarity) || p.Lacunarity < 1 || p.Lacunarity > 8 || !finite(p.Gain) || p.Gain < 0 || p.Gain > 1 {
		return fmt.Errorf("noise: invalid profile (octaves 1..32, frequency (0,1e6], lacunarity 1..8, gain 0..1)")
	}
	return nil
}
func (p Profile) MarshalJSON() ([]byte, error) {
	if err := p.Validate(); err != nil {
		return nil, err
	}
	type plain Profile
	return json.Marshal(plain(p))
}

type Sampler struct {
	generator Generator
	profile   Profile
}

func NewProfile(p Profile) (Sampler, error) {
	if err := p.Validate(); err != nil {
		return Sampler{}, err
	}
	return Sampler{New(p.Seed), p}, nil
}
func (s Sampler) Sample2(x, y float64) (float64, error) { return s.Sample3(x, y, 0) }
func (s Sampler) Sample3(x, y, z float64) (float64, error) {
	if err := s.profile.Validate(); err != nil {
		return 0, err
	}
	if !validCoordinate(x) || !validCoordinate(y) || !validCoordinate(z) {
		return 0, fmt.Errorf("noise: invalid input coordinates")
	}
	frequency, amplitude, total, weight := s.profile.Frequency, 1., 0., 0.
	for octave := 0; octave < s.profile.Octaves; octave++ {
		if amplitude == 0 {
			break
		}
		v, err := s.generator.Sample3(x*frequency, y*frequency, z*frequency)
		if err != nil {
			return 0, fmt.Errorf("noise: octave %d: %w", octave, err)
		}
		total += v * amplitude
		weight += amplitude
		frequency *= s.profile.Lacunarity
		amplitude *= s.profile.Gain
	}
	return total / weight, nil
}
