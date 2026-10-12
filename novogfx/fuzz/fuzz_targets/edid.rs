#![no_main]
#![forbid(unsafe_op_in_unsafe_fn)]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    let _ = novogfx_kms::Edid::parse(data);
});
