package native_extension_api

import "testing"

func TestSessionCallbacksAndFailedReload(t *testing.T) {
	s := New(1000)
	defer s.Close()
	fn := Function{Name: "double", Arity: 1, Call: func(args []Value) (Value, error) { return Value{Kind: Int, Integer: args[0].Integer * 2}, nil }}
	if err := s.Register([]Function{fn}); err != nil {
		t.Fatal(err)
	}
	if err := s.Register([]Function{fn}); err == nil {
		t.Fatal("duplicate accepted")
	}
	if err := s.Load(`fn main() { return double(21); }`); err != nil {
		t.Fatal(err)
	}
	if v, err := s.Call("main", nil); err != nil || v.Integer != 42 {
		t.Fatalf("%v %v", v, err)
	}
	if err := s.Load(`const X: i64 = missing(); fn main() { return 0; }`); err == nil {
		t.Fatal("bad initializer accepted")
	}
	if v, err := s.Call("main", nil); err != nil || v.Integer != 42 {
		t.Fatalf("reload broke prior module: %v %v", v, err)
	}
	if err := s.Load(`fn main() { loop {} }`); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Call("main", nil); ErrorCode(err) != RuntimeError {
		t.Fatalf("budget: %v", err)
	}
}

func TestReentryAndScalarValidation(t *testing.T) {
	s := New(100)
	err := s.Register([]Function{{Name: "reenter", Call: func([]Value) (Value, error) {
		if _, err := s.Call("main", nil); ErrorCode(err) != Busy {
			t.Fatalf("reentry: %v", err)
		}
		return Value{Kind: Bool, Integer: 1}, nil
	}}})
	if err != nil {
		t.Fatal(err)
	}
	if err := s.Load(`fn main() { return reenter(); }`); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Call("main", nil); err != nil {
		t.Fatal(err)
	}
	if _, err := toRuntime(Value{Kind: Bool, Integer: 2}); err == nil {
		t.Fatal("invalid bool")
	}
	if err := s.Close(); err != nil {
		t.Fatal(err)
	}
	if _, err := s.Call("main", nil); ErrorCode(err) != InvalidHandle {
		t.Fatalf("closed: %v", err)
	}
}

func TestRegisterRejectsBuiltinCollision(t *testing.T) {
	s := New(100)
	err := s.Register([]Function{{Name: "print", Arity: 0, Call: func([]Value) (Value, error) {
		return Value{}, nil
	}}})
	if ErrorCode(err) != InvalidArgument {
		t.Fatalf("builtin collision status = %v, err = %v", ErrorCode(err), err)
	}
}

func TestConcurrentCloseAndCall(t *testing.T) {
	s := New(100)
	defer s.Close()
	entered, release := make(chan struct{}), make(chan struct{})
	if err := s.Register([]Function{{Name: "wait", Call: func([]Value) (Value, error) {
		close(entered)
		<-release
		return Value{Kind: Int, Integer: 42}, nil
	}}}); err != nil {
		t.Fatal(err)
	}
	done := make(chan error, 1)
	if err := s.Load(`fn main() { return wait(); }`); err != nil {
		t.Fatal(err)
	}
	go func() { _, err := s.Call("main", nil); done <- err }()
	select {
	case <-entered:
	case err := <-done:
		t.Fatalf("callback was not reached: %v", err)
	}
	_, callErr := s.Call("wait", nil)
	closeErr := s.Close()
	close(release)
	if err := <-done; err != nil {
		t.Fatal(err)
	}
	if ErrorCode(callErr) != Busy || ErrorCode(closeErr) != Busy {
		t.Fatalf("concurrent operations: call=%v close=%v", callErr, closeErr)
	}
}
