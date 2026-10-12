//! Create a dumb buffer, map it, and fill it with a gradient.
//!
//! Usage: `cargo run -p novogfx --example dumb_buffer -- /dev/dri/card0`

use novogfx::prelude::*;

fn main() -> std::result::Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/dri/card0".to_string());
    let device = Device::open(&path)?;
    let mut buf = DumbBuffer::new(&device, 640, 480, 32)?;
    println!(
        "dumb buffer: {}x{} pitch={} size={} fb={}",
        buf.width(),
        buf.height(),
        buf.pitch(),
        buf.size(),
        buf.framebuffer_id()
    );
    // Write a horizontal gradient (XRGB8888).
    let pitch = buf.pitch() as usize;
    let (w, h) = (buf.width() as usize, buf.height() as usize);
    let slice = buf.as_mut_slice();
    for y in 0..h {
        for x in 0..w {
            let r = (x * 255 / w.max(1)) as u32;
            let g = (y * 255 / h.max(1)) as u32;
            let b = 128u32;
            let pixel = (r << 16) | (g << 8) | b;
            let off = y * pitch + x * 4;
            slice[off..off + 4].copy_from_slice(&pixel.to_le_bytes());
        }
    }
    println!("gradient written (respects pitch {pitch})");
    Ok(())
}
