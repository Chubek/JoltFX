import java.nio.file.Path;
class Smoke {
    public static void main(String[] args) {
        try (var vm=new IngagiModule(Path.of(args[0]),1000)) {
            vm.load("fn main(x: i64) { return x + 1; }");
            if (vm.call("main",IngagiModule.Scalar.integer(41)).integer()!=42) throw new AssertionError();
            try { vm.load("fn {"); throw new AssertionError(); }
            catch (IngagiModule.IngagiException e) { if (e.status!=IngagiModule.COMPILE_ERROR) throw e; }
        }
    }
}
