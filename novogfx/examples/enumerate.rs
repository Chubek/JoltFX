//! Enumerate DRM resources: connectors, CRTCs, encoders, planes.
//!
//! Usage: `cargo run -p novogfx --example enumerate -- /dev/dri/card0`

use novogfx::prelude::*;
use std::os::fd::{AsFd, AsRawFd};

fn main() -> std::result::Result<(), Box<dyn std::error::Error + Send + Sync>> {
    let path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/dri/card0".to_string());
    let device = Device::open(&path)?;
    let info = device.driver()?;
    println!(
        "driver: {} {}.{}.{} ({})",
        info.name, info.major, info.minor, info.patch, info.date
    );
    println!("caps: {:#x}", device.capabilities().bits());

    let res = device.resources()?.clone();
    println!(
        "resources: {} connectors, {} crtcs, {} encoders, {} planes, {} fbs",
        res.connectors.len(),
        res.crtcs.len(),
        res.encoders.len(),
        res.planes.len(),
        res.framebuffers.len()
    );

    for id in &res.connectors {
        let conn = novogfx::Connector::get(&device, novogfx::ConnectorId(*id))?;
        println!(
            "connector {}: {:?} {:?} ({} modes)",
            id,
            conn.connector_type(),
            conn.status(),
            conn.modes().len()
        );
        for m in conn.modes() {
            println!(
                "  mode {}x{}@{} {}",
                m.hdisplay,
                m.vdisplay,
                m.refresh_millihertz() / 1000,
                if m.is_preferred() { "(preferred)" } else { "" }
            );
        }
        if let Some(edid) = conn.edid() {
            println!(
                "  EDID: {} product {:#x}",
                edid.manufacturer, edid.product_code
            );
        }
    }
    for id in &res.crtcs {
        let crtc = novogfx::Crtc::get(&device, novogfx::CrtcId(*id))?;
        println!(
            "crtc {id}: fb={:?} mode={}",
            crtc.current_framebuffer(),
            crtc.current_mode()
                .map_or("(off)".to_string(), |m| m.name.clone())
        );
    }
    for id in &res.planes {
        let mut plane = novogfx::Plane::get(&device, novogfx::PlaneId(*id))?;
        let _ = plane.refresh_type();
        println!(
            "plane {id}: type={:?} crtcs={:#x} formats={} fb={:?}",
            plane.plane_type(),
            plane.possible_crtcs(),
            plane.formats().len(),
            plane.current_fb()
        );
    }
    println!("fd: {}", device.as_fd().as_raw_fd());
    Ok(())
}
