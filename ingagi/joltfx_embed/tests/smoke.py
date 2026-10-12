import importlib.util
import sys

spec = importlib.util.spec_from_file_location("ingagi_module", sys.argv[1])
api = importlib.util.module_from_spec(spec)
spec.loader.exec_module(api)
with api.Session(sys.argv[2], budget=1000) as vm:
    def reenter():
        try:
            vm.close()
            raise AssertionError("callback reentry accepted")
        except api.IngagiError as e:
            assert e.status == api.BUSY
        return 42
    vm.register("reenter", 0, reenter)
    vm.load("fn main() { return reenter(); }")
    assert vm.call("main") == 42
    vm.register("twice", 1, lambda n: n * 2)
    vm.load("fn main(x: i64) { return twice(x); }")
    assert vm.call("main", 21) == 42
    try:
        vm.load("fn {")
        raise AssertionError("accepted invalid source")
    except api.IngagiError as e:
        assert e.status == api.COMPILE_ERROR
    assert vm.call("main", 21) == 42
    vm.load("fn main(x: bool) { return x; }")
    assert vm.call("main", True) is True
    vm.load("fn main(x: f64) { return x + 0.5; }")
    assert vm.call("main", 41.5) == 42.0
    vm.load("fn main() { return; }")
    assert vm.call("main") is None
    vm.register("oops", 0, lambda: 1 / 0)
    vm.load("fn main() { return oops(); }")
    try:
        vm.call("main")
        raise AssertionError("callback exception lost")
    except api.IngagiError as e:
        assert e.status == api.RUNTIME_ERROR and "division" in str(e)
    vm.load("fn main() { loop {} }")
    try:
        vm.call("main")
        raise AssertionError("budget was not enforced")
    except api.IngagiError as e:
        assert e.status == api.RUNTIME_ERROR and "budget" in str(e)
print("Python scalar/callback/error/budget checks passed")
