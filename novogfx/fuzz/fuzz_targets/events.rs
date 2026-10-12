#![no_main]
#![forbid(unsafe_op_in_unsafe_fn)]

use libfuzzer_sys::fuzz_target;

fuzz_target!(|data: &[u8]| {
    if let Ok((_, consumed)) = novogfx_event::reader::parse_events_checked(data, u64::MAX) {
        assert!(consumed <= data.len());
    }
    let (_, consumed) = novogfx_event::reader::parse_events(data, u64::MAX);
    assert!(consumed <= data.len());
    // Repeat after a truncated prefix, mirroring partial reads.
    let (_, prefix) = novogfx_event::reader::parse_events(&data[..data.len() / 2], 0);
    let _ = novogfx_event::reader::parse_events(&data[prefix..], 0);
});
