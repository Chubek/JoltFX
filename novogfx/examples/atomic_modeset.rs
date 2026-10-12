//! Atomic modeset: bind the first connected connector to a CRTC.
//!
//! Usage: `cargo run -p novogfx --example atomic_modeset -- /dev/dri/card0`
//!
//! This example test-commits only by default; pass `--commit` to apply.

use novogfx::prelude::*;

fn main() -> std::result::Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let args: Vec<String> = std::env::args().collect();
    let path = args
        .get(1)
        .cloned()
        .unwrap_or_else(|| "/dev/dri/card0".to_string());
    let do_commit = args.iter().any(|a| a == "--commit");
    let mut device = Device::open(&path)?;
    device.enable_atomic()?;
    let res = device.resources()?.clone();
    // Find the first connected connector.
    let mut chosen = None;
    for id in &res.connectors {
        let conn = novogfx::Connector::get(&device, novogfx::ConnectorId(*id))?;
        if conn.status() == ConnectorStatus::Connected {
            chosen = Some((
                *id,
                conn.preferred_mode()
                    .cloned()
                    .or_else(|| conn.modes().first().cloned()),
            ));
            break;
        }
    }
    let (conn_id, mode) = match chosen {
        Some((id, Some(m))) => (id, m),
        _ => {
            println!("no connected connector with modes; nothing to do");
            return Ok(());
        }
    };
    let crtc_id = res.crtcs.first().copied().ok_or("no CRTCs")?;
    // Allocate a dumb buffer for the mode resolution.
    let mut dumb = DumbBuffer::new(
        &device,
        u32::from(mode.hdisplay),
        u32::from(mode.vdisplay),
        32,
    )?;
    dumb.fill_solid(0x00_20_40_80, 4)?;
    let fb = dumb.framebuffer_id().raw();
    println!(
        "connector {conn_id} -> crtc {crtc_id} mode {} fb {fb}",
        mode.name
    );
    let mut req = AtomicRequest::new(&mut device);
    req.set_crtc(crtc_id, Some(fb), Some(&mode), Some(conn_id))?;
    if do_commit {
        req.commit(CommitFlags::ALLOW_MODESET)?;
        println!("committed");
    } else {
        // Test-only: validates without touching hardware.
        match req.commit(CommitFlags::TEST_ONLY | CommitFlags::ALLOW_MODESET) {
            Ok(()) => println!("test-commit OK (pass --commit to apply)"),
            Err(e) => println!("test-commit failed: {e}"),
        }
    }
    Ok(())
}
