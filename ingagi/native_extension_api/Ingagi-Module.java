/* Java 22+ Foreign Function & Memory binding, 64-bit hosts.
 * Install as IngagiModule.java; no JNI/JNA dependency. Run with
 * --enable-native-access=ALL-UNNAMED. The source filename supplied by the
 * repository is legal because the top-level class is package-private.
 * Hosts may place this class in their own package before distribution.
 */
import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.concurrent.locks.ReentrantLock;

final class IngagiModule implements AutoCloseable {
    public static final int ABI_VERSION = 0x010000;
    public static final int OK=0, INVALID_ARGUMENT=1, INVALID_HANDLE=2,
        COMPILE_ERROR=3, RUNTIME_ERROR=4, UNSUPPORTED=5, BUSY=6, INTERNAL_ERROR=7;
    public static final int NIL=0, BOOL=1, INT=2, FLOAT=3;
    public static final StructLayout VALUE = MemoryLayout.structLayout(
        ValueLayout.JAVA_INT.withName("kind"), ValueLayout.JAVA_INT.withName("reserved"),
        ValueLayout.JAVA_LONG.withName("integer"), ValueLayout.JAVA_DOUBLE.withName("real"));
    public static final StructLayout FUNCTION = MemoryLayout.structLayout(
        ValueLayout.ADDRESS.withName("name"), ValueLayout.JAVA_LONG.withName("name_size"),
        ValueLayout.JAVA_INT.withName("arity"), ValueLayout.JAVA_INT.withName("reserved"),
        ValueLayout.ADDRESS.withName("callback"), ValueLayout.ADDRESS.withName("userdata"));
    public static final StructLayout MODULE = MemoryLayout.structLayout(
        ValueLayout.JAVA_INT.withName("abi_version"), ValueLayout.JAVA_INT.withName("function_count"),
        ValueLayout.ADDRESS.withName("functions"));
    public static final FunctionDescriptor CALLBACK = FunctionDescriptor.of(ValueLayout.JAVA_INT,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS, ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG);

    public record Scalar(int kind, long integer, double real) {
        public static Scalar nil() { return new Scalar(NIL,0,0); }
        public static Scalar integer(long n) { return new Scalar(INT,n,0); }
        public static Scalar bool(boolean b) { return new Scalar(BOOL,b ? 1 : 0,0); }
        public static Scalar real(double n) { return new Scalar(FLOAT,0,n); }
        void write(MemorySegment segment) {
            segment.set(ValueLayout.JAVA_INT,0,kind);
            segment.set(ValueLayout.JAVA_INT,4,0);
            segment.set(ValueLayout.JAVA_LONG,8,integer);
            segment.set(ValueLayout.JAVA_DOUBLE,16,real);
        }
        static Scalar read(MemorySegment s) {
            return new Scalar(s.get(ValueLayout.JAVA_INT,0),s.get(ValueLayout.JAVA_LONG,8),s.get(ValueLayout.JAVA_DOUBLE,16));
        }
    }
    public static final class IngagiException extends RuntimeException {
        public final int status;
        IngagiException(int status, String message) { super(message); this.status=status; }
    }
    // Go c-shared runtimes cannot be safely unloaded while runtime threads live.
    // Keep the library process-resident; close() releases only the VM handle.
    private final Arena libraryArena = Arena.global();
    private final MethodHandle create, destroy, load, call, register;
    private long handle;
    private boolean closed;
    private final ReentrantLock gate = new ReentrantLock();
    private static MethodHandle bind(SymbolLookup symbols,String name,MemoryLayout... args) {
        return Linker.nativeLinker().downcallHandle(symbols.find(name).orElseThrow(),
            FunctionDescriptor.of(ValueLayout.JAVA_INT,args));
    }
    private static int invoke(MethodHandle fn, Object... args) {
        try { return (int)fn.invokeWithArguments(args); }
        catch (RuntimeException | Error e) { throw e; }
        catch (Throwable e) { throw new IllegalStateException("foreign call failed",e); }
    }
    private static void check(int status,MemorySegment error) {
        if (status!=OK) throw new IngagiException(status,error.getString(0));
    }
    public IngagiModule(Path library,long budget) {
        try {
            if (ValueLayout.ADDRESS.byteSize()!=8) throw new UnsupportedOperationException("Java binding requires a 64-bit host");
            if (budget<0) throw new IllegalArgumentException("budget must be nonnegative");
            var symbols=SymbolLookup.libraryLookup(library,libraryArena);
            if (invoke(bind(symbols,"ingagi_abi_version"))!=ABI_VERSION) throw new IllegalStateException("Ingagi ABI mismatch");
            var L=ValueLayout.JAVA_LONG; var P=ValueLayout.ADDRESS; var I=ValueLayout.JAVA_INT;
            create=bind(symbols,"ingagi_create",L,P,P,L);
            destroy=bind(symbols,"ingagi_destroy",L,P,L);
            load=bind(symbols,"ingagi_load",L,P,L,P,L);
            call=bind(symbols,"ingagi_call",L,P,L,P,I,P,P,L);
            register=bind(symbols,"ingagi_register_module",L,P,P,L);
            try (var arena=Arena.ofConfined()) {
                var out=arena.allocate(L); var error=arena.allocate(4096);
                check(invoke(create,budget,out,error,4096L),error);
                handle=out.get(L,0);
            }
        } catch (RuntimeException | Error e) { throw e; }
    }
    private void enter() {
        if (gate.isHeldByCurrentThread() || !gate.tryLock()) throw new IngagiException(BUSY,"session is busy");
        if (closed) { gate.unlock(); throw new IllegalStateException("session closed"); }
    }
    public void load(String source) {
        enter();
        try (var arena=Arena.ofConfined()) {
            var bytes=source.getBytes(StandardCharsets.UTF_8);
            var input=arena.allocateFrom(ValueLayout.JAVA_BYTE,bytes); var error=arena.allocate(4096);
            check(invoke(load,handle,input,(long)bytes.length,error,4096L),error);
        } finally { gate.unlock(); }
    }
    public Scalar call(String name,Scalar... args) {
        if (args.length>256) throw new IllegalArgumentException("too many arguments");
        enter();
        try (var arena=Arena.ofConfined()) {
            var bytes=name.getBytes(StandardCharsets.UTF_8);
            var input=arena.allocateFrom(ValueLayout.JAVA_BYTE,bytes);
            var values=arena.allocate(Math.max(1,args.length)*VALUE.byteSize(),VALUE.byteAlignment());
            for (int i=0;i<args.length;i++) args[i].write(values.asSlice(i*VALUE.byteSize(),VALUE.byteSize()));
            var result=arena.allocate(VALUE); var error=arena.allocate(4096);
            check(invoke(call,handle,input,(long)bytes.length,values,args.length,result,error,4096L),error);
            return Scalar.read(result);
        } finally { gate.unlock(); }
    }
    // descriptor uses MODULE/FUNCTION layouts above. Keep upcall stubs and
    // userdata alive until close; callback exceptions must be caught in Java.
    public void registerModule(MemorySegment descriptor) {
        enter();
        try (var arena=Arena.ofConfined()) {
            var error=arena.allocate(4096);
            check(invoke(register,handle,descriptor,error,4096L),error);
        } finally { gate.unlock(); }
    }
    @Override public void close() {
        if (gate.isHeldByCurrentThread() || !gate.tryLock()) throw new IngagiException(BUSY,"session is busy");
        try {
            if (closed) return;
            try (var arena=Arena.ofConfined()) {
                var error=arena.allocate(4096);
                check(invoke(destroy,handle,error,4096L),error);
            }
            handle=0; closed=true;
        } finally { gate.unlock(); }
    }
}
