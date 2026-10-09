package runtime

import (
	"testing"

	fe "github.com/Chubek/JoltFX/ingagi/frontend"
)

func TestNestedFunctionState(t *testing.T) {
	cases := []struct {
		name, src string
		want      int64
		nilResult bool
	}{
		{"transitive capture", `fn maker(n: i32) {
            return |x: i32| { return |y: i32| { return n + x + y; }; };
        }
        fn main() { let f = maker(10); let g = f(20); return g(12); }`, 42, false},
		{"outer question before lambda", `fn main() {
            let n = null?;
            let f = |x: i32| { return x?; };
            return 99;
        }`, 0, true},
		{"outer loop after lambda", `fn main() {
            let n = 0;
            while n < 3 {
                let f = |x: i32| { return x + 1; };
                n = f(n);
                if n == 2 { break; }
            }
            return n;
        }`, 2, false},
		{"capture through question", `fn maker(n: i32) {
            return |x: i32| { return n? + x; };
        }
        fn main() { let f = maker(40); return f(2); }`, 42, false},
		{"local declaration state", `fn main() {
            let n = null?;
            fn helper() { return 1; }
            return 99;
        }`, 0, true},
	}
	for _, tc := range cases {
		for _, optimized := range []bool{false, true} {
			name := tc.name + "/raw"
			if optimized {
				name = tc.name + "/optimized"
			}
			t.Run(name, func(t *testing.T) {
				r := fe.Parse(tc.src)
				if !r.OK() {
					t.Fatal(r.Errors)
				}
				m, errs := Compile(r.File)
				if len(errs) != 0 {
					t.Fatal(errs)
				}
				if optimized {
					m.Tape = Optimize(m.Tape)
				}
				vm := New()
				if err := vm.LoadModule(m); err != nil {
					t.Fatal(err)
				}
				got, err := vm.CallBudget(Budget{MaxInstrs: 10000}, "main")
				if err != nil {
					t.Fatal(err)
				}
				if tc.nilResult {
					if !got.IsNil() {
						t.Fatalf("got %v, want nil", got)
					}
				} else if got.K != KInt || got.I != tc.want {
					t.Fatalf("got %v, want %d", got, tc.want)
				}
			})
		}
	}
}

func TestLambdaCannotBreakEnclosingLoop(t *testing.T) {
	r := fe.Parse(`fn main() { loop { let f = |x: i32| { break; }; break; } }`)
	if !r.OK() {
		t.Fatal(r.Errors)
	}
	_, errs := Compile(r.File)
	if len(errs) == 0 {
		t.Fatal("lambda break incorrectly resolved to enclosing loop")
	}
}
