package monkeytrace

import (
	"math"
	"sort"
)

// Nodes are preorder-threaded: skip names the first node after this subtree.
// Internal nodes enter their left child at node+1. Leaves name one primitive in
// entity-ID order, so equal-distance hits remain stable regardless of tree shape.
// Traversal is stackless on both CPU and GPU and visits each node at most once.
type bvhNode struct {
	min, max        Vec3
	skip, primitive int
}

func component(v Vec3, axis int) float64 {
	switch axis {
	case 0:
		return v.X
	case 1:
		return v.Y
	default:
		return v.Z
	}
}
func minVec(a, b Vec3) Vec3 { return Vec3{math.Min(a.X, b.X), math.Min(a.Y, b.Y), math.Min(a.Z, b.Z)} }
func maxVec(a, b Vec3) Vec3 { return Vec3{math.Max(a.X, b.X), math.Max(a.Y, b.Y), math.Max(a.Z, b.Z)} }
func gpuVec(v Vec3) Vec3 {
	return Vec3{float64(float32(v.X)), float64(float32(v.Y)), float64(float32(v.Z))}
}

func primitiveBounds(p primitive) bvhNode {
	var low, high Vec3
	if p.Radius > 0 {
		r := Vec3{p.Radius, p.Radius, p.Radius}
		low, high = p.A.sub(r), p.A.add(r)
		gp, gr := gpuVec(p.A), float64(float32(p.Radius))
		r = Vec3{gr, gr, gr}
		low, high = minVec(low, gp.sub(r)), maxVec(high, gp.add(r))
	} else {
		low = minVec(p.A, minVec(p.B, p.C))
		high = maxVec(p.A, maxVec(p.B, p.C))
		low = minVec(low, minVec(gpuVec(p.A), minVec(gpuVec(p.B), gpuVec(p.C))))
		high = maxVec(high, maxVec(gpuVec(p.A), maxVec(gpuVec(p.B), gpuVec(p.C))))
	}
	// Include both double and quantized geometry. Expand thin/planar bounds and
	// round outward, rather than rounding a min upward or a max downward on upload.
	down := func(v float64) float64 { return float64(math.Nextafter32(float32(v-epsilon*4), float32(math.Inf(-1)))) }
	up := func(v float64) float64 { return float64(math.Nextafter32(float32(v+epsilon*4), float32(math.Inf(1)))) }
	return bvhNode{min: Vec3{down(low.X), down(low.Y), down(low.Z)}, max: Vec3{up(high.X), up(high.Y), up(high.Z)}, primitive: -1}
}

func (f *Frame) buildBVH() {
	f.nodes = nil
	if len(f.primitives) == 0 {
		return
	}
	indices := make([]int, len(f.primitives))
	boxes := make([]bvhNode, len(indices))
	for i, p := range f.primitives {
		indices[i] = i
		boxes[i] = primitiveBounds(p)
	}
	f.nodes = make([]bvhNode, 0, len(indices)*2-1)
	var build func([]int)
	build = func(ids []int) {
		index := len(f.nodes)
		bounds := boxes[ids[0]]
		for _, id := range ids[1:] {
			bounds.min = minVec(bounds.min, boxes[id].min)
			bounds.max = maxVec(bounds.max, boxes[id].max)
		}
		f.nodes = append(f.nodes, bounds)
		if len(ids) == 1 {
			f.nodes[index].primitive = ids[0]
		} else {
			size := bounds.max.sub(bounds.min)
			axis := 0
			if size.Y > size.X {
				axis = 1
			}
			if size.Z > component(size, axis) {
				axis = 2
			}
			sort.Slice(ids, func(i, j int) bool {
				a := component(boxes[ids[i]].min, axis) + component(boxes[ids[i]].max, axis)
				b := component(boxes[ids[j]].min, axis) + component(boxes[ids[j]].max, axis)
				if a == b {
					return ids[i] < ids[j]
				}
				return a < b
			})
			middle := len(ids) / 2
			build(ids[:middle])
			build(ids[middle:])
		}
		f.nodes[index].skip = len(f.nodes)
	}
	build(indices)
}

func intersectsBounds(n bvhNode, eye, dir Vec3, limit float64) bool {
	near, far := epsilon, limit
	for axis := 0; axis < 3; axis++ {
		origin, d := component(eye, axis), component(dir, axis)
		low, high := component(n.min, axis), component(n.max, axis)
		if d == 0 {
			if origin < low || origin > high {
				return false
			}
			continue
		}
		a, b := (low-origin)/d, (high-origin)/d
		if a > b {
			a, b = b, a
		}
		near, far = math.Max(near, a), math.Min(far, b)
		if near > far {
			return false
		}
	}
	return true
}

// The primitive-test count supports differential/pruning tests and benchmarks.
func (f *Frame) traverse(eye, dir Vec3, anyHit bool) (float64, int, int) {
	best, hit, tests := noHit, -1, 0
	for index := 0; index < len(f.nodes); {
		node := f.nodes[index]
		if !intersectsBounds(node, eye, dir, best) {
			index = node.skip
			continue
		}
		if node.primitive >= 0 {
			tests++
			distance := intersect(f.primitives[node.primitive], eye, dir)
			if distance < best || (distance == best && distance < noHit && node.primitive < hit) {
				best, hit = distance, node.primitive
				if anyHit {
					return best, hit, tests
				}
			}
		}
		index++
	}
	return best, hit, tests
}

func (f *Frame) closestHit(eye, dir Vec3) (float64, int) {
	distance, hit, _ := f.traverse(eye, dir, false)
	return distance, hit
}
func (f *Frame) occluded(eye, dir Vec3) bool {
	_, hit, _ := f.traverse(eye, dir, true)
	return hit >= 0
}

// BVHData returns a fresh std430 buffer for binding 3. Two vec4s per node encode
// min.xyz/skip and max.xyz/primitive (-1 for internal nodes). Unused slots are
// zeroed; ParameterData records the actual node count in sampling.y.
func (f *Frame) BVHData() []float32 {
	data := make([]float32, MaxBVHNodes*8)
	for i, n := range f.nodes {
		values := []float64{n.min.X, n.min.Y, n.min.Z, float64(n.skip), n.max.X, n.max.Y, n.max.Z, float64(n.primitive)}
		for j, v := range values {
			data[i*8+j] = float32(v)
		}
	}
	return data
}
