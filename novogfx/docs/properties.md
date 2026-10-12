# KMS properties — novogfx

Well-known DRM property names recognised by `novogfx-kms::property::well_known`
and the typed accessors built on top.

| Name | Object(s) | Type | Notes |
|------|-----------|------|-------|
| `ACTIVE` | CRTC | range 0–1 | Head enabled. Set by `AtomicRequest::set_crtc`. |
| `MODE_ID` | CRTC | blob | Mode blob created per-commit, destroyed on every exit path. Never cached across commits. |
| `FB_ID` | CRTC, plane | object | Framebuffer binding. `0`/`NULL` disables. |
| `CRTC_ID` | connector, plane | object | Routing binding. `0`/`NULL` unbinds. |
| `SRC_X/Y/W/H` | plane | range | Source crop in 16.16 fixed point. |
| `CRTC_X/Y/W/H` | plane | range/signed | Destination rect; X/Y are signed. |
| `type` | plane | enum (immutable) | `primary`/`overlay`/`cursor`. Resolved by `Plane::refresh_type`. |
| `alpha` | plane | range | Opacity. Typed accessor: `Plane::alpha()`. |
| `zpos` | plane | range | Stacking order. Set via `PlaneUpdate::zpos`. |
| `IN_FORMATS` | plane | blob | Supported format/modifier list. Created per-commit only. |
| `EDID` | connector | blob | Raw EDID; parsed by `novogfx-kms::edid` (`Edid::parse`). |
| `DPMS` | connector | enum | Display Power Management Signaling. |

## Adding a property (agent checklist)

1. Add the name to `novogfx-kms::property::well_known`.
2. Add a typed accessor on the relevant wrapper (e.g. `Plane::alpha()`).
3. Test with a property mock in `novogfx-kms::property::tests`.
4. Update this file.
