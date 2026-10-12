"""ctypes binding to Ingagi ABI 1.0.0 (install as ingagi_module.py).

Use Session('/absolute/path/libingagi_embed.so') as a context manager. Scalar
values are None/bool/signed-int64/float. Registered Python callables are retained
until close; callback exceptions become script runtime errors.
"""
import ctypes as C
import functools
import threading

ABI_VERSION = 0x010000
OK, INVALID_ARGUMENT, INVALID_HANDLE, COMPILE_ERROR, RUNTIME_ERROR, UNSUPPORTED, BUSY, INTERNAL_ERROR = range(8)
NIL, BOOL, INT, FLOAT = range(4)


class Value(C.Structure):
    _fields_ = [("kind", C.c_uint32), ("reserved", C.c_uint32),
                ("integer", C.c_int64), ("real", C.c_double)]

    @classmethod
    def encode(cls, value):
        if value is None:
            return cls()
        if isinstance(value, bool):
            return cls(BOOL, 0, int(value), 0)
        if isinstance(value, int):
            if not -(1 << 63) <= value < (1 << 63):
                raise OverflowError("Ingagi integer must fit int64")
            return cls(INT, 0, value, 0)
        if isinstance(value, float):
            return cls(FLOAT, 0, 0, value)
        raise TypeError("Ingagi ABI supports None, bool, int and float")

    def decode(self):
        if self.reserved:
            raise ValueError("invalid reserved field")
        if self.kind == NIL:
            return None
        if self.kind == BOOL and self.integer in (0, 1):
            return bool(self.integer)
        if self.kind == INT:
            return self.integer
        if self.kind == FLOAT:
            return self.real
        raise ValueError("invalid scalar from Ingagi")


Callback = C.CFUNCTYPE(C.c_int32, C.c_void_p, C.POINTER(Value), C.c_uint32,
                      C.POINTER(Value), C.POINTER(C.c_char), C.c_size_t)


class Function(C.Structure):
    _fields_ = [("name", C.c_char_p), ("name_size", C.c_size_t),
                ("arity", C.c_uint32), ("reserved", C.c_uint32),
                ("callback", Callback), ("userdata", C.c_void_p)]


class Module(C.Structure):
    _fields_ = [("abi_version", C.c_uint32), ("function_count", C.c_uint32),
                ("functions", C.POINTER(Function))]


class IngagiError(RuntimeError):
    def __init__(self, status, message):
        super().__init__(message)
        self.status = status


def _exclusive(method):
    @functools.wraps(method)
    def guarded(self, *args, **kwargs):
        if not self._gate.acquire(blocking=False):
            raise IngagiError(BUSY, "session is busy")
        try:
            return method(self, *args, **kwargs)
        finally:
            self._gate.release()
    return guarded


class Session:
    def __init__(self, library, budget=1_000_000):
        if not isinstance(budget, int) or not 0 <= budget < 1 << 64:
            raise ValueError("budget must fit uint64")
        self._lib = C.CDLL(str(library))
        self._handle = C.c_uint64()
        self._callbacks = []
        self._gate = threading.Lock()
        ptr = C.POINTER(C.c_char)
        signatures = {
            "ingagi_create": [C.c_uint64, C.POINTER(C.c_uint64), ptr, C.c_size_t],
            "ingagi_destroy": [C.c_uint64, ptr, C.c_size_t],
            "ingagi_load": [C.c_uint64, C.c_char_p, C.c_size_t, ptr, C.c_size_t],
            "ingagi_call": [C.c_uint64, C.c_char_p, C.c_size_t, C.POINTER(Value), C.c_uint32, C.POINTER(Value), ptr, C.c_size_t],
            "ingagi_register_module": [C.c_uint64, C.POINTER(Module), ptr, C.c_size_t],
        }
        for name, args in signatures.items():
            fn = getattr(self._lib, name)
            fn.argtypes, fn.restype = args, C.c_int32
        self._lib.ingagi_abi_version.argtypes = []
        self._lib.ingagi_abi_version.restype = C.c_uint32
        if self._lib.ingagi_abi_version() != ABI_VERSION:
            raise IngagiError(UNSUPPORTED, "Ingagi ABI mismatch")
        self._invoke("ingagi_create", budget, C.byref(self._handle))

    def _invoke(self, name, *args):
        error = C.create_string_buffer(4096)
        code = getattr(self._lib, name)(*args, error, len(error))
        if code:
            raise IngagiError(code, error.value.decode("utf-8", "replace"))

    @_exclusive
    def load(self, source):
        data = source.encode("utf-8")
        self._invoke("ingagi_load", self._handle, data, len(data))

    @_exclusive
    def call(self, name, *args):
        if len(args) > 256:
            raise ValueError("too many arguments")
        data = name.encode("utf-8")
        values = (Value * len(args))(*(Value.encode(a) for a in args))
        result = Value()
        self._invoke("ingagi_call", self._handle, data, len(data), values, len(args), C.byref(result))
        return result.decode()

    @_exclusive
    def register(self, name, arity, function):
        if not isinstance(arity, int) or not 0 <= arity <= 256 or not callable(function):
            raise ValueError("invalid callback or arity")
        data = name.encode("utf-8")

        @Callback
        def callback(_userdata, args, argc, result, error, capacity):
            try:
                result[0] = Value.encode(function(*(args[i].decode() for i in range(argc))))
                return OK
            except BaseException as exc:
                if capacity:
                    message = str(exc).encode("utf-8", "replace")[:capacity - 1] + b"\0"
                    C.memmove(error, message, len(message))
                return RUNTIME_ERROR

        descriptor = Function(data, len(data), arity, 0, callback, None)
        module = Module(ABI_VERSION, 1, C.pointer(descriptor))
        self._invoke("ingagi_register_module", self._handle, C.byref(module))
        self._callbacks.append(callback)

    @_exclusive
    def close(self):
        if self._handle.value:
            self._invoke("ingagi_destroy", self._handle)
            self._handle.value = 0
            self._callbacks.clear()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


assert C.sizeof(Value) == 24
