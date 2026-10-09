//! Creative-programming sketch layer over the JBC1 kernel subset.
//!
//! A sketch is still one `(defkernel name [inputs...] body)` program, so it
//! compiles through the same [`crate::validate`] + [`crate::bytecode`] path
//! (and stays byte-identical to the Glue Layer C compiler). The sketch layer
//! adds the conventions that make per-pixel creative coding viable:
//!
//! * required `x y t` bindings (normalized coords + seconds), optional `mx my`
//!   (normalized pointer), all declared in the `defkernel` input list;
//! * optional `;; @sketch`, `;; @canvas`, `;; @fps`, `;; @duration` metadata;
//! * a small report type the CLI and the desktop Creative tab share.

use std::collections::HashMap;

use crate::validate::{validate_source, KernelInfo};

/// Parsed sketch metadata with creative defaults.
#[derive(Debug, Clone, PartialEq)]
pub struct SketchInfo {
    pub kernel: KernelInfo,
    pub canvas_width: u32,
    pub canvas_height: u32,
    pub fps: f64,
    pub duration: f64,
    pub interactive: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SketchError {
    pub line: usize,
    pub column: usize,
    pub message: String,
}

impl std::fmt::Display for SketchError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}:{}: {}", self.line, self.column, self.message)
    }
}

fn sketch_meta(source: &str) -> HashMap<String, String> {
    let mut meta = HashMap::new();
    for line in source.lines() {
        let trimmed = line.trim_start();
        let body = trimmed.strip_prefix(";;").unwrap_or("").trim_start();
        if let Some(rest) = body.strip_prefix('@') {
            let mut parts = rest.splitn(2, char::is_whitespace);
            let key = parts.next().unwrap_or("").to_string();
            let value = parts.next().unwrap_or("").trim().to_string();
            if !key.is_empty() {
                meta.entry(key).or_insert(value);
            }
        }
    }
    meta
}

fn parse_canvas(value: &str) -> Option<(u32, u32)> {
    let mut parts = value.split(['x', 'X', ' ', ',']);
    let w: u32 = parts.next()?.trim().parse().ok()?;
    let h: u32 = parts.next()?.trim().parse().ok()?;
    if w == 0 || h == 0 || w > 2048 || h > 2048 {
        return None;
    }
    Some((w, h))
}

/// Validate a creative sketch: a valid kernel plus the `x y t` convention.
pub fn validate_sketch(source: &str) -> Result<SketchInfo, SketchError> {
    let fail = |message: &str| SketchError {
        line: 1,
        column: 1,
        message: message.to_string(),
    };
    let info = validate_source(source).map_err(|d| SketchError {
        line: d.line,
        column: d.column,
        message: d.message,
    })?;
    for required in ["x", "y", "t"] {
        if !info.inputs.iter().any(|b| b == required) {
            return Err(fail(&format!(
                "creative sketch must declare '{required}' in its input list (found [{}]); see `zoltan stdlib show sketch`",
                info.inputs.join(" ")
            )));
        }
    }
    let meta = sketch_meta(source);
    let (canvas_width, canvas_height) = meta
        .get("canvas")
        .and_then(|v| parse_canvas(v))
        .unwrap_or((320, 180));
    let fps: f64 = meta.get("fps").and_then(|v| v.parse().ok()).unwrap_or(30.0);
    if !fps.is_finite() || fps <= 0.0 || fps > 120.0 {
        return Err(fail("invalid @fps: expected a finite rate in (0, 120]"));
    }
    let duration: f64 = meta
        .get("duration")
        .and_then(|v| v.parse().ok())
        .unwrap_or(10.0);
    if !duration.is_finite() || duration <= 0.0 || duration > 600.0 {
        return Err(fail(
            "invalid @duration: expected finite seconds in (0, 600]",
        ));
    }
    let interactive = info.inputs.iter().any(|b| b == "mx" || b == "my");
    Ok(SketchInfo {
        kernel: info,
        canvas_width,
        canvas_height,
        fps,
        duration,
        interactive,
    })
}

/// Evaluate the compiled program on the CPU for `frames` sketch frames.
///
/// Each output pixel runs the JBC1 stack machine with inputs mapped from the
/// sketch bindings: `x`/`y` are normalized coordinates, `t` is seconds,
/// `mx`/`my` default to 0.5 when present, and any other binding reads 0.0.
/// This is a preview path only; the desktop tab renders through the engine.
pub fn run_sketch_frames(
    program: &[u8],
    info: &SketchInfo,
    width: u32,
    height: u32,
    frames: usize,
) -> Result<Vec<u8>, String> {
    let (inputs, outputs) =
        crate::bytecode::header(program).map_err(|e| format!("invalid program: {e}"))?;
    if outputs != 1 && outputs != 4 {
        return Err("sketch must emit 1 or 4 outputs".to_string());
    }
    let w = width.min(info.canvas_width).max(1) as usize;
    let h = height.min(info.canvas_height).max(1) as usize;
    let names = info.kernel.inputs.clone();
    let mut rgba = vec![0u8; frames * w * h * 4];
    for frame in 0..frames {
        let t = frame as f64 / info.fps;
        for y in 0..h {
            for x in 0..w {
                let mut values = vec![0f32; inputs as usize];
                for (i, name) in names.iter().enumerate() {
                    values[i] = match name.as_str() {
                        "x" => (x as f32) / ((w.max(2) - 1) as f32),
                        "y" => (y as f32) / ((h.max(2) - 1) as f32),
                        "t" => t as f32,
                        "mx" | "my" => 0.5,
                        _ => 0.0,
                    };
                }
                let out = eval_program(program, &values, outputs as usize)?;
                let base = (frame * w * h + y * w + x) * 4;
                for c in 0..4 {
                    let v = if outputs == 1 { out[0] } else { out[c] };
                    rgba[base + c] = (v.clamp(0.0, 1.0) * 255.0).round() as u8;
                }
            }
        }
    }
    Ok(rgba)
}

fn eval_program(program: &[u8], inputs: &[f32], outputs: usize) -> Result<[f32; 4], String> {
    let mut stack: Vec<f32> = Vec::with_capacity(64);
    let mut offset = crate::bytecode::HEADER_SIZE;
    let mut out = [0f32; 4];
    while offset + 8 <= program.len() {
        let op = u32::from_le_bytes(program[offset..offset + 4].try_into().unwrap());
        let operand = u32::from_le_bytes(program[offset + 4..offset + 8].try_into().unwrap());
        offset += 8;
        match op {
            1 => stack.push(f32::from_bits(operand)),
            2 => stack.push(*inputs.get(operand as usize).unwrap_or(&0.0)),
            3 | 4 | 5 | 6 | 7 | 8 | 11 | 13 | 16 | 17 | 18 | 19 | 20 | 21 | 22 | 24 | 25 | 26
            | 27 | 28 => {
                if stack.len() < 2 {
                    return Err("stack underflow".to_string());
                }
                let b = stack.pop().unwrap();
                let a = stack.pop().unwrap();
                let (ai, bi) = (a.to_bits(), b.to_bits());
                stack.push(match op {
                    3 => a + b,
                    4 => a - b,
                    5 => a * b,
                    6 => a / b,
                    7 => a.min(b),
                    8 => a.max(b),
                    11 => a.powf(b),
                    13 => {
                        if a < b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    16 => {
                        if a > b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    17 => {
                        if a <= b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    18 => {
                        if a >= b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    19 => {
                        if a == b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    20 => {
                        if a != b {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    21 => {
                        if a != 0.0 && b != 0.0 {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    22 => {
                        if a != 0.0 || b != 0.0 {
                            1.0
                        } else {
                            0.0
                        }
                    }
                    24 => f32::from_bits(ai & bi),
                    25 => f32::from_bits(ai | bi),
                    26 => f32::from_bits(ai ^ bi),
                    27 => f32::from_bits(ai.wrapping_shl(bi & 31)),
                    _ => f32::from_bits(ai.wrapping_shr(bi & 31)),
                });
            }
            9 | 10 | 12 | 23 => {
                let a = stack.pop().ok_or("stack underflow")?;
                stack.push(match op {
                    9 => a.abs(),
                    10 => a.floor(),
                    12 => a.sqrt(),
                    _ => {
                        if a == 0.0 {
                            1.0
                        } else {
                            0.0
                        }
                    }
                });
            }
            14 => {
                if stack.len() < 3 {
                    return Err("stack underflow".to_string());
                }
                let c = stack.pop().unwrap();
                let b = stack.pop().unwrap();
                let a = stack.pop().unwrap();
                stack.push(if a != 0.0 { b } else { c });
            }
            15 => {
                let v = stack.pop().ok_or("stack underflow")?;
                if (operand as usize) < outputs {
                    out[operand as usize] = v;
                }
            }
            _ => return Err(format!("unknown opcode {op}")),
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    const SKETCH: &str = ";; @kernel demo\n;; @category generative\n\
        ;; @description Demo.\n;; @complexity Low\n;; @gpu Yes\n;; @since 0.3.0\n\
        ;; @canvas 320x180\n;; @fps 30\n;; @duration 2\n\
        (defkernel demo [x y t] (rgba x y t 1.0))";

    #[test]
    fn valid_sketch_reports_canvas() {
        let info = validate_sketch(SKETCH).expect("valid sketch");
        assert_eq!((info.canvas_width, info.canvas_height), (320, 180));
        assert!(!info.interactive);
    }

    #[test]
    fn missing_time_binding_fails_with_hint() {
        let src = SKETCH
            .replace("[x y t]", "[x y]")
            .replace("(rgba x y t 1.0)", "(rgba x y 0.5 1.0)");
        let err = validate_sketch(&src).unwrap_err();
        assert!(err.message.contains("must declare 't'"), "{err}");
    }

    #[test]
    fn preview_frames_have_expected_size() {
        let info = validate_sketch(SKETCH).expect("valid");
        let program = crate::bytecode::compile(SKETCH).expect("compile");
        let pixels = run_sketch_frames(&program, &info, 8, 4, 2).expect("run");
        assert_eq!(pixels.len(), 2 * 8 * 4 * 4);
    }
}
