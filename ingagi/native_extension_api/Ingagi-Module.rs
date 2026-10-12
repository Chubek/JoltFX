//! Ingagi ABI 1.0.0. Include with #[path = "Ingagi-Module.rs"] mod ingagi_module;
//! or compile as crate `ingagi_module`. Link with -L/path/to/library.
use std::ffi::{c_char, c_void};
use std::marker::PhantomData;
use std::rc::Rc;

pub const ABI_VERSION: u32 = 0x010000;
pub const NIL: u32 = 0;
pub const BOOL: u32 = 1;
pub const INT: u32 = 2;
pub const FLOAT: u32 = 3;
pub const OK: i32 = 0;
pub const INVALID_ARGUMENT: i32 = 1;
pub const INVALID_HANDLE: i32 = 2;
pub const COMPILE_ERROR: i32 = 3;
pub const RUNTIME_ERROR: i32 = 4;
pub const UNSUPPORTED: i32 = 5;
pub const BUSY: i32 = 6;
pub const INTERNAL_ERROR: i32 = 7;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default)]
pub struct Value { pub kind: u32, pub reserved: u32, pub integer: i64, pub real: f64 }
impl Value {
    pub fn int(v: i64) -> Self { Self { kind: INT, integer: v, ..Self::default() } }
    pub fn boolean(v: bool) -> Self { Self { kind: BOOL, integer: i64::from(v), ..Self::default() } }
    pub fn float(v: f64) -> Self { Self { kind: FLOAT, real: v, ..Self::default() } }
}
pub type Callback = unsafe extern "C" fn(*mut c_void, *const Value, u32, *mut Value, *mut c_char, usize) -> i32;
#[repr(C)]
pub struct Function {
    pub name: *const c_char, pub name_size: usize, pub arity: u32, pub reserved: u32,
    pub callback: Option<Callback>, pub userdata: *mut c_void,
}
#[repr(C)]
pub struct Module { pub abi_version: u32, pub function_count: u32, pub functions: *const Function }

#[link(name = "ingagi_embed")]
extern "C" {
    pub fn ingagi_abi_version() -> u32;
    pub fn ingagi_create(budget: u64, result: *mut u64, error: *mut c_char, capacity: usize) -> i32;
    pub fn ingagi_destroy(handle: u64, error: *mut c_char, capacity: usize) -> i32;
    pub fn ingagi_load(handle: u64, source: *const c_char, size: usize, error: *mut c_char, capacity: usize) -> i32;
    pub fn ingagi_call(handle: u64, name: *const c_char, size: usize, args: *const Value, argc: u32,
        result: *mut Value, error: *mut c_char, capacity: usize) -> i32;
    pub fn ingagi_register_module(handle: u64, module: *const Module, error: *mut c_char, capacity: usize) -> i32;
}

#[derive(Debug)]
pub struct Error { pub status: i32, pub message: String }
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result { write!(f, "{} (Ingagi {})", self.message, self.status) }
}
impl std::error::Error for Error {}
fn check(status: i32, message: &[u8]) -> Result<(), Error> {
    if status == OK { return Ok(()); }
    let end = message.iter().position(|&b| b == 0).unwrap_or(message.len());
    Err(Error { status, message: String::from_utf8_lossy(&message[..end]).into_owned() })
}

// Owning, single-threaded wrapper. Raw ABI callers may use the native Busy gate.
pub struct Session { handle: u64, _not_send_sync: PhantomData<Rc<()>> }
impl Session {
    pub fn new(budget: u64) -> Result<Self, Error> {
        if unsafe { ingagi_abi_version() } != ABI_VERSION {
            return Err(Error { status: UNSUPPORTED, message: "Ingagi ABI mismatch".into() });
        }
        let mut handle = 0; let mut message = [0u8; 4096];
        check(unsafe { ingagi_create(budget, &mut handle, message.as_mut_ptr().cast(), message.len()) }, &message)?;
        Ok(Self { handle, _not_send_sync: PhantomData })
    }
    pub fn native_handle(&self) -> u64 { self.handle }
    pub fn load(&mut self, source: &str) -> Result<(), Error> {
        let mut message = [0u8; 4096];
        check(unsafe { ingagi_load(self.handle, source.as_ptr().cast(), source.len(), message.as_mut_ptr().cast(), message.len()) }, &message)
    }
    pub fn call(&mut self, name: &str, args: &[Value]) -> Result<Value, Error> {
        if args.len() > 256 { return Err(Error { status: INVALID_ARGUMENT, message: "too many arguments".into() }); }
        let mut message = [0u8; 4096]; let mut out = Value::default();
        check(unsafe { ingagi_call(self.handle, name.as_ptr().cast(), name.len(), args.as_ptr(), args.len() as u32,
            &mut out, message.as_mut_ptr().cast(), message.len()) }, &message)?;
        Ok(out)
    }
    /// # Safety
    /// Descriptor pointers must be valid; callback code and userdata must live
    /// until this session closes. Callbacks must never unwind into foreign code.
    pub unsafe fn register_module(&mut self, module: &Module) -> Result<(), Error> {
        let mut message = [0u8; 4096];
        check(unsafe { ingagi_register_module(self.handle, module, message.as_mut_ptr().cast(), message.len()) }, &message)
    }
    pub fn close(&mut self) -> Result<(), Error> {
        if self.handle == 0 { return Ok(()); }
        let mut message = [0u8; 4096];
        check(unsafe { ingagi_destroy(self.handle, message.as_mut_ptr().cast(), message.len()) }, &message)?;
        self.handle = 0; Ok(())
    }
}
impl Drop for Session { fn drop(&mut self) { let _ = self.close(); } }
const _: () = assert!(std::mem::size_of::<Value>() == 24);
