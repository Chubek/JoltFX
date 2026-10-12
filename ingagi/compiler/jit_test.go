package compiler

import (
	"context"
	"os"
	"strings"
	"testing"
)

func TestJITValidation(t *testing.T) {
	f := parseNative(t, `fn main() -> i64 { return 42; }`)
	for _, options := range []NativeOptions{{Emit: "obj"}, {Target: "wasm32"}, {Entry: "missing"}} {
		if _, err := BuildJIT(context.Background(), f, options); err == nil {
			t.Fatalf("accepted %+v", options)
		}
	}
	if _, err := BuildJIT(nil, f, NativeOptions{}); err == nil {
		t.Fatal("accepted nil context")
	}
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if _, err := BuildJIT(ctx, f, NativeOptions{}); err != context.Canceled {
		t.Fatal(err)
	}
	if _, err := (*NativeFunction)(nil).Call(); err == nil {
		t.Fatal("accepted nil function")
	}
	if _, err := BuildJIT(context.Background(), parseNative(t, `fn main(a:i64,b:i64,c:i64,d:i64,e:i64,f:i64,g:i64,h:i64,i:i64) -> i64 { return i; }`), NativeOptions{}); err == nil || !strings.Contains(err.Error(), "eight") {
		t.Fatalf("oversized arity: %v", err)
	}
	if err := (*NativeFunction)(nil).Close(); err != nil {
		t.Fatal(err)
	}
}

func TestJITExecution(t *testing.T) {
	if !JITAvailable() || os.Getenv("INGAGI_QBE") == "" || os.Getenv("INGAGI_KEYSTONE_LIBRARY") == "" {
		t.Skip("requires Linux amd64 CGO, INGAGI_QBE and INGAGI_KEYSTONE_LIBRARY")
	}
	for _, tc := range []struct {
		source string
		args   []int64
		want   int64
	}{
		{`fn main() -> i64 { return 42; }`, nil, 42},
		{`fn main(n: i64) -> i64 { if n < 2 { return n; } return main(n-1)+main(n-2); }`, []int64{15}, 610},
		{`fn main(a: i64,b: i64,c: i64,d: i64,e: i64,f: i64,g: i64,h: i64) -> i64 { return a+b+c+d+e+f+g+h; }`, []int64{1, 2, 3, 4, 5, 6, 7, 14}, 42},
		{`fn main(x: bool, y: i64) -> i64 { if x { return y; } return -1; }`, []int64{1 << 40, 42}, 42},
		{`fn main(x: bool) -> bool { return !x; }`, []int64{0}, 1},
	} {
		fn, err := BuildJIT(context.Background(), parseNative(t, tc.source), NativeOptions{})
		if err != nil {
			t.Fatal(err)
		}
		t.Cleanup(func() {
			if err := fn.Close(); err != nil {
				t.Error(err)
			}
		})
		for i := 0; i < 3; i++ {
			got, err := fn.Call(tc.args...)
			if err != nil || got != tc.want {
				t.Fatalf("got %d: %v, want %d", got, err, tc.want)
			}
		}
		if _, err := fn.Call(append(tc.args, 0)...); err == nil {
			t.Fatal("accepted wrong arity")
		}
		if err := fn.Close(); err != nil {
			t.Fatal(err)
		}
		if _, err := fn.Call(tc.args...); err == nil || !strings.Contains(err.Error(), "closed") {
			t.Fatalf("closed call: %v", err)
		}
	}
}
