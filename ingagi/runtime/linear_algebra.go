package runtime

import (
	"fmt"
	"math"
)

func registerLinearAlgebra(vm *VM) {
	vm.builtins["mat4"] = bMat4
	vm.builtins["transpose"] = bTranspose
	vm.builtins["determinant"] = bDeterminant
	vm.builtins["inverse"] = bInverse
	vm.builtins["length"] = bLength
}

func vectorSize(k Kind) int {
	switch k {
	case KVec2:
		return 2
	case KVec3:
		return 3
	case KVec4:
		return 4
	}
	return 0
}

func finiteMat(v Value) (Mat4, error) {
	m, err := matArg(v)
	if err != nil {
		return Mat4{}, err
	}
	for _, f := range m {
		if math.IsNaN(f) || math.IsInf(f, 0) {
			return Mat4{}, fmt.Errorf("linear algebra: nonfinite matrix")
		}
	}
	return m, nil
}

func bMat4(vm *VM, args []Value) (Value, error) {
	var m Mat4
	switch len(args) {
	case 0:
		m = IdentityMat4()
	case 1:
		s, err := proceduralNumber(args[0])
		if err != nil {
			return NilValue, err
		}
		for i := 0; i < 4; i++ {
			m[i*5] = s
		}
	case 4:
		for c, v := range args {
			if v.K != KVec4 {
				return NilValue, vm.fail("mat4 takes four vec4 columns")
			}
			copy(m[c*4:], v.V[:])
		}
	default:
		return NilValue, vm.fail("mat4 takes (), (diagonal), or four vec4 columns")
	}
	v := Value{K: KMat4, O: m}
	_, err := finiteMat(v)
	return v, err
}

func matrixOperand(vm *VM, name string, args []Value) (Mat4, error) {
	if len(args) != 1 {
		return Mat4{}, vm.fail("%s takes one mat4", name)
	}
	return finiteMat(args[0])
}

func bTranspose(vm *VM, args []Value) (Value, error) {
	m, err := matrixOperand(vm, "transpose", args)
	if err != nil {
		return NilValue, err
	}
	return Value{K: KMat4, O: transposeMat4(m)}, nil
}
func bDeterminant(vm *VM, args []Value) (Value, error) {
	m, err := matrixOperand(vm, "determinant", args)
	if err != nil {
		return NilValue, err
	}
	d := determinantMat4(m)
	if math.IsNaN(d) || math.IsInf(d, 0) {
		return NilValue, vm.fail("determinant is nonfinite")
	}
	return FloatValue(d), nil
}
func bInverse(vm *VM, args []Value) (Value, error) {
	m, err := matrixOperand(vm, "inverse", args)
	if err != nil {
		return NilValue, err
	}
	d := determinantMat4(m)
	if d == 0 || math.IsNaN(d) || math.IsInf(d, 0) {
		return NilValue, vm.fail("inverse requires a finite nonsingular matrix")
	}
	v := Value{K: KMat4, O: inverseMat4(m)}
	if _, err = finiteMat(v); err != nil {
		return NilValue, err
	}
	return v, nil
}

func vectorLength(v Value) (float64, error) {
	n := vectorSize(v.K)
	if v.K == KQuat {
		n = 4
	}
	if n == 0 {
		return 0, fmt.Errorf("linear algebra: expected vector or quaternion")
	}
	l := 0.0
	for _, f := range v.V[:n] {
		if math.IsNaN(f) || math.IsInf(f, 0) {
			return 0, fmt.Errorf("linear algebra: nonfinite component")
		}
		l = math.Hypot(l, f)
	}
	if math.IsInf(l, 0) {
		return 0, fmt.Errorf("linear algebra: length overflow")
	}
	return l, nil
}
func bLength(vm *VM, args []Value) (Value, error) {
	if len(args) != 1 {
		return NilValue, vm.fail("length takes one vector or quaternion")
	}
	l, err := vectorLength(args[0])
	if err != nil {
		return NilValue, err
	}
	return FloatValue(l), nil
}

func scalarTransposeMat4(m Mat4) (out Mat4) {
	for c := 0; c < 4; c++ {
		for r := 0; r < 4; r++ {
			out.Set(c, r, m.At(r, c))
		}
	}
	return
}
func cofactorMat4(m Mat4, col, row int) float64 {
	var a [9]float64
	i := 0
	for r := 0; r < 4; r++ {
		if r != row {
			for c := 0; c < 4; c++ {
				if c != col {
					a[i] = m.At(c, r)
					i++
				}
			}
		}
	}
	d := a[0]*(a[4]*a[8]-a[5]*a[7]) - a[1]*(a[3]*a[8]-a[5]*a[6]) + a[2]*(a[3]*a[7]-a[4]*a[6])
	if (col+row)%2 != 0 {
		d = -d
	}
	return d
}
func scalarDeterminantMat4(m Mat4) (d float64) {
	for c := 0; c < 4; c++ {
		d += m.At(c, 0) * cofactorMat4(m, c, 0)
	}
	return
}
func scalarInverseMat4(m Mat4) (out Mat4) {
	d := scalarDeterminantMat4(m)
	for c := 0; c < 4; c++ {
		for r := 0; r < 4; r++ {
			out.Set(c, r, cofactorMat4(m, r, c)/d)
		}
	}
	return
}
