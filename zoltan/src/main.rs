//! Zoltan compiler + creative-programming toolchain.
//!
//! `compile`/`verify` validate a Joltscript kernel (see [`validate`]) and emit
//! the real JBC1 bytecode (see [`bytecode`]). `new`/`run`/`export`/`stdlib`
//! add the creative-sketch layer (see [`sketch`], [`stdlib`]): a sketch is
//! still one `(defkernel ...)` program, so it stays byte-identical to the
//! Glue Layer C compiler, with the `x y t` (+ optional `mx my`) convention
//! and `@canvas`/`@fps`/`@duration` metadata on top.

mod bytecode;
mod sketch;
mod stdlib;
mod validate;

use std::env;
use std::fs;
use std::process::ExitCode;

use validate::validate_source;

const VERSION: &str = "0.3.0";

fn usage() -> String {
    format!(
        "zoltan {VERSION} - JoltScript compiler and creative-programming tool\n\
         \n\
         Usage: zoltan <command> [options]\n\
         \n\
         Commands:\n\
         \x20 compile FILE [-o OUTPUT] [--emit-metadata]  Validate a kernel and emit JBC1 bytecode\n\
         \x20 verify FILE                                 Validate a kernel without writing output\n\
         \x20 new NAME [--dir DIR]                        Scaffold a creative sketch\n\
         \x20 run FILE [--frames N] [--width W] [--height H]  Preview a sketch on the CPU\n\
         \x20 export FILE --as obj|html|wasm [-o OUTPUT]  Export a sketch project\n\
         \x20 stdlib list|show NAME                       List or print stdlib snippets\n\
         \x20 help [COMMAND]                              Show help\n\
         \n\
         Global options:\n\
         \x20 --version    Show version information\n\
         \x20 --help       Show this help message\n"
    )
}

fn compile_help() -> &'static str {
    "Usage: zoltan compile FILE [-o OUTPUT] [--emit-metadata]\n\
     \n\
     Validates a Joltscript kernel (single `(defkernel name [inputs...] body)`\n\
     form, scalar f32 expressions) and, with -o, writes the compiled JBC1\n\
     bytecode. Without -o nothing is written and the kernel is only checked.\n\
     \n\
     The emitted bytecode is byte-identical to the Glue Layer compiler's, so\n\
     the output runs anywhere JoltFX does. Exits non-zero with a\n\
     file:line:column diagnostic on failure.\n"
}

fn read_source(path: &str) -> Result<String, String> {
    fs::read_to_string(path).map_err(|e| format!("error: cannot read '{path}': {e}"))
}

fn cmd_compile(args: &[String]) -> ExitCode {
    let mut input: Option<&str> = None;
    let mut output: Option<&str> = None;
    let mut emit_metadata = false;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "-o" | "--output" => {
                i += 1;
                match args.get(i) {
                    Some(path) => output = Some(path),
                    None => {
                        eprintln!(
                            "error: {0} requires a value\n\n{1}",
                            args[i - 1],
                            compile_help()
                        );
                        return ExitCode::FAILURE;
                    }
                }
            }
            "--emit-metadata" => emit_metadata = true,
            "-h" | "--help" => {
                print!("{0}", compile_help());
                return ExitCode::SUCCESS;
            }
            other if other.starts_with('-') => {
                eprintln!("error: unknown option '{other}'\n\n{0}", compile_help());
                return ExitCode::FAILURE;
            }
            path => {
                if input.is_some() {
                    eprintln!(
                        "error: expected one FILE, got several\n\n{0}",
                        compile_help()
                    );
                    return ExitCode::FAILURE;
                }
                input = Some(path);
            }
        }
        i += 1;
    }
    let Some(path) = input else {
        eprintln!("error: missing FILE\n\n{0}", compile_help());
        return ExitCode::FAILURE;
    };
    let source = match read_source(path) {
        Ok(source) => source,
        Err(message) => {
            eprintln!("{message}");
            return ExitCode::FAILURE;
        }
    };
    let info = match validate_source(&source) {
        Ok(info) => info,
        Err(diagnostic) => {
            eprintln!("error: {path}:{diagnostic}");
            return ExitCode::FAILURE;
        }
    };
    let program = match bytecode::compile(&source) {
        Ok(program) => program,
        Err(error) => {
            eprintln!("error: {path}:{error}");
            return ExitCode::FAILURE;
        }
    };
    if let Err(error) = bytecode::header(&program) {
        eprintln!("error: {path}: internal error: emitted an invalid program: {error}");
        return ExitCode::FAILURE;
    }
    if let Some(out) = output {
        if let Err(e) = fs::write(out, &program) {
            eprintln!("error: cannot write '{out}': {e}");
            return ExitCode::FAILURE;
        }
    }
    let (declared_inputs, declared_outputs) = bytecode::header(&program).unwrap_or((0, 0));
    if declared_inputs as usize != info.inputs.len() || declared_outputs != info.outputs {
        eprintln!(
            "error: {path}: internal error: emitted {} inputs/{} outputs, validator said {}/{}",
            declared_inputs,
            declared_outputs,
            info.inputs.len(),
            info.outputs
        );
        return ExitCode::FAILURE;
    }
    println!(
        "ok: {path}: {} ({} inputs, {} outputs, {} bytes of JBC1)",
        info.name,
        info.inputs.len(),
        info.outputs,
        program.len()
    );
    if emit_metadata {
        println!("kernel={}", info.name);
        println!("category={}", info.category);
        println!("inputs={}", info.inputs.join(","));
        println!("outputs={}", info.outputs);
    }
    ExitCode::SUCCESS
}

fn cmd_verify(args: &[String]) -> ExitCode {
    if args.len() != 1 || args[0] == "-h" || args[0] == "--help" {
        eprintln!("Usage: zoltan verify FILE");
        return if args.len() == 1 {
            ExitCode::SUCCESS
        } else {
            ExitCode::FAILURE
        };
    }
    let path = &args[0];
    let source = match read_source(path) {
        Ok(source) => source,
        Err(message) => {
            eprintln!("{message}");
            return ExitCode::FAILURE;
        }
    };
    match validate_source(&source) {
        Ok(info) => {
            println!(
                "ok: {path}: {} ({} inputs, {} outputs)",
                info.name,
                info.inputs.len(),
                info.outputs
            );
            ExitCode::SUCCESS
        }
        Err(diagnostic) => {
            eprintln!("error: {path}:{diagnostic}");
            ExitCode::FAILURE
        }
    }
}

fn cmd_new(args: &[String]) -> ExitCode {
    let mut name: Option<&str> = None;
    let mut dir: Option<&str> = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--dir" => {
                i += 1;
                match args.get(i) {
                    Some(d) => dir = Some(d),
                    None => {
                        eprintln!("error: --dir requires a value");
                        return ExitCode::FAILURE;
                    }
                }
            }
            other if other.starts_with('-') => {
                eprintln!("error: unknown option '{other}'\nUsage: zoltan new NAME [--dir DIR]");
                return ExitCode::FAILURE;
            }
            n => {
                if name.is_some() {
                    eprintln!("error: expected one NAME");
                    return ExitCode::FAILURE;
                }
                name = Some(n);
            }
        }
        i += 1;
    }
    let Some(name) = name else {
        eprintln!("Usage: zoltan new NAME [--dir DIR]");
        return ExitCode::FAILURE;
    };
    if !name
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '_' || c == '-')
        || name.is_empty()
    {
        eprintln!("error: NAME must be [A-Za-z0-9_-]+");
        return ExitCode::FAILURE;
    }
    let safe = name.replace('-', "_");
    let root = match dir {
        Some(d) => std::path::PathBuf::from(d).join(name),
        None => std::path::PathBuf::from(name),
    };
    let source = format!(
        ";; @kernel {safe}\n;; @category generative\n\
         ;; @description Creative sketch scaffold.\n;; @complexity Low\n\
         ;; @gpu Yes\n;; @since 0.3.0\n;; @canvas 320x180\n;; @fps 30\n;; @duration 10\n\
         ;; Paste stdlib snippets: `zoltan stdlib list` / `zoltan stdlib show circle`.\n\
         (defkernel {safe} [x y t]\n  (rgba x y (* 0.5 (+ 0.5 (* 0.5 t))) 1.0))\n"
    );
    if let Err(e) = fs::create_dir_all(&root) {
        eprintln!("error: cannot create '{}': {e}", root.display());
        return ExitCode::FAILURE;
    }
    let path = root.join("sketch.jolt");
    if path.exists() {
        eprintln!("error: '{}' already exists", path.display());
        return ExitCode::FAILURE;
    }
    if let Err(e) = fs::write(&path, &source) {
        eprintln!("error: cannot write '{}': {e}", path.display());
        return ExitCode::FAILURE;
    }
    // Scaffold must validate so `zoltan run` works immediately.
    if let Err(d) = validate_source(&source) {
        eprintln!("error: internal error: scaffold does not validate: {d}");
        return ExitCode::FAILURE;
    }
    println!("created {}", path.display());
    ExitCode::SUCCESS
}

fn cmd_run(args: &[String]) -> ExitCode {
    let mut file: Option<&str> = None;
    let mut frames = 4usize;
    let mut width = 64u32;
    let mut height = 36u32;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--frames" => {
                i += 1;
                match args.get(i).and_then(|v| v.parse().ok()) {
                    Some(n) => frames = n,
                    None => {
                        eprintln!("error: --frames requires an integer");
                        return ExitCode::FAILURE;
                    }
                }
            }
            "--width" => {
                i += 1;
                match args.get(i).and_then(|v| v.parse().ok()) {
                    Some(n) => width = n,
                    None => {
                        eprintln!("error: --width requires an integer");
                        return ExitCode::FAILURE;
                    }
                }
            }
            "--height" => {
                i += 1;
                match args.get(i).and_then(|v| v.parse().ok()) {
                    Some(n) => height = n,
                    None => {
                        eprintln!("error: --height requires an integer");
                        return ExitCode::FAILURE;
                    }
                }
            }
            other if other.starts_with('-') => {
                eprintln!("error: unknown option '{other}'");
                return ExitCode::FAILURE;
            }
            p => {
                if file.is_some() {
                    eprintln!("error: expected one FILE");
                    return ExitCode::FAILURE;
                }
                file = Some(p);
            }
        }
        i += 1;
    }
    let Some(path) = file else {
        eprintln!("Usage: zoltan run FILE [--frames N] [--width W] [--height H]");
        return ExitCode::FAILURE;
    };
    if frames == 0 || frames > 16 {
        eprintln!("error: --frames must be in 1..=16 (preview bound)");
        return ExitCode::FAILURE;
    }
    if width == 0 || width > 320 || height == 0 || height > 180 {
        eprintln!("error: preview size is bounded to 320x180");
        return ExitCode::FAILURE;
    }
    let source = match read_source(path) {
        Ok(s) => s,
        Err(m) => {
            eprintln!("{m}");
            return ExitCode::FAILURE;
        }
    };
    let info = match sketch::validate_sketch(&source) {
        Ok(info) => info,
        Err(e) => {
            eprintln!("error: {path}:{e}");
            return ExitCode::FAILURE;
        }
    };
    let program = match bytecode::compile(&source) {
        Ok(p) => p,
        Err(e) => {
            eprintln!("error: {path}:{e}");
            return ExitCode::FAILURE;
        }
    };
    match sketch::run_sketch_frames(&program, &info, width, height, frames) {
        Ok(pixels) => {
            // Deterministic checksum so `run` is testable without images.
            let mut sum: u64 = 0;
            for b in &pixels {
                sum = sum.wrapping_add(*b as u64);
            }
            println!(
                "ok: {path}: {} frames {}x{} checksum {sum}",
                frames,
                width.min(info.canvas_width),
                height.min(info.canvas_height)
            );
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: {path}: {e}");
            ExitCode::FAILURE
        }
    }
}

fn write_minimal_elf(program: &[u8], kernel: &str, out: &str) -> Result<(), String> {
    // Minimal ELF64 relocatable with a single `.jolt` section carrying the
    // JBC1 payload. This is an interchange wrapper, not a native code object:
    // it lets creative projects round-trip as one file without building LIEF.
    // A LIEF-based inspector can read `.jolt` back with `lief.parse()`.
    let name_table: &[u8] = b"\0.shstrtab\0.jolt\0";
    let shstr_off = 64u64;
    let jolt_off = shstr_off + name_table.len() as u64;
    let jolt_size = program.len() as u64;
    let sh_off = (jolt_off + jolt_size + 7) & !7;
    let mut elf: Vec<u8> = Vec::with_capacity((sh_off + 3 * 64) as usize);
    let mut header = [0u8; 64];
    header[0..4].copy_from_slice(b"\x7fELF");
    header[4] = 2;
    header[5] = 1;
    header[6] = 1;
    header[16..18].copy_from_slice(&1u16.to_le_bytes());
    header[18..20].copy_from_slice(&62u16.to_le_bytes());
    header[20..24].copy_from_slice(&1u32.to_le_bytes());
    header[40..48].copy_from_slice(&sh_off.to_le_bytes());
    header[48..52].copy_from_slice(&0u32.to_le_bytes());
    header[52..54].copy_from_slice(&64u16.to_le_bytes());
    header[54..56].copy_from_slice(&0u16.to_le_bytes());
    header[56..58].copy_from_slice(&0u16.to_le_bytes());
    header[58..60].copy_from_slice(&64u16.to_le_bytes());
    header[60..62].copy_from_slice(&3u16.to_le_bytes());
    header[62..64].copy_from_slice(&1u16.to_le_bytes());
    elf.extend_from_slice(&header);
    let _ = kernel;
    elf.extend_from_slice(name_table);
    while (elf.len() as u64) < jolt_off {
        elf.push(0);
    }
    elf.extend_from_slice(program);
    while (elf.len() as u64) < sh_off {
        elf.push(0);
    }
    let mut section = [0u8; 64];
    // Null section.
    elf.extend_from_slice(&section);
    // .shstrtab: name=1, type STRTAB(3).
    section[0..4].copy_from_slice(&1u32.to_le_bytes());
    section[4..8].copy_from_slice(&3u32.to_le_bytes());
    section[24..32].copy_from_slice(&shstr_off.to_le_bytes());
    section[32..40].copy_from_slice(&(name_table.len() as u64).to_le_bytes());
    elf.extend_from_slice(&section);
    section = [0u8; 64];
    // .jolt: name=11, type PROGBITS(1).
    section[0..4].copy_from_slice(&11u32.to_le_bytes());
    section[4..8].copy_from_slice(&1u32.to_le_bytes());
    section[24..32].copy_from_slice(&jolt_off.to_le_bytes());
    section[32..40].copy_from_slice(&jolt_size.to_le_bytes());
    elf.extend_from_slice(&section);
    fs::write(out, &elf).map_err(|e| format!("cannot write '{out}': {e}"))?;
    Ok(())
}

fn transpile_to_js(source: &str) -> Result<String, String> {
    // Reuse the bytecode compiler as the syntax gate, then lower the
    // instruction stream to JS. Only straight-line JBC1 scalar code is
    // supported; image-profile forms are rejected with a clear error.
    let program = bytecode::compile(source).map_err(|e| e.to_string())?;
    let (inputs, _) = bytecode::header(&program).map_err(|e| e.to_string())?;
    let mut js = String::from("(x,y,t,mx,my)=>{const s=[];const inp=[x,y,t,mx,my];");
    js.push_str(&format!("while(inp.length<{inputs})inp.push(0);"));
    let mut offset = bytecode::HEADER_SIZE;
    while offset + 8 <= program.len() {
        let op = u32::from_le_bytes(program[offset..offset + 4].try_into().unwrap());
        let operand = u32::from_le_bytes(program[offset + 4..offset + 8].try_into().unwrap());
        offset += 8;
        match op {
            1 => js.push_str(&format!("s.push({});", f32::from_bits(operand))),
            2 => js.push_str(&format!("s.push(inp[{operand}]);")),
            3 => js.push_str("s.push(s.pop()+s.pop());"),
            4 => js.push_str("{const b=s.pop(),a=s.pop();s.push(a-b);}"),
            5 => js.push_str("s.push(s.pop()*s.pop());"),
            6 => js.push_str("{const b=s.pop(),a=s.pop();s.push(a/b);}"),
            7 => js.push_str("s.push(Math.min(s.pop(),s.pop()));"),
            8 => js.push_str("s.push(Math.max(s.pop(),s.pop()));"),
            9 => js.push_str("s.push(Math.abs(s.pop()));"),
            10 => js.push_str("s.push(Math.floor(s.pop()));"),
            11 => js.push_str("{const b=s.pop(),a=s.pop();s.push(Math.pow(a,b));}"),
            12 => js.push_str("s.push(Math.sqrt(s.pop()));"),
            13 => js.push_str("{const b=s.pop(),a=s.pop();s.push(a<b?1:0);}"),
            14 => js.push_str("{const c=s.pop(),b=s.pop(),a=s.pop();s.push(a!==0?b:c);}"),
            15 => js.push_str(&format!("if({operand}===0)out0=s.pop();")),
            _ => return Err(format!("HTML export: opcode {op} not lowered yet")),
        }
    }
    js.push_str("return out0;}");
    Ok(js)
}

fn cmd_export(args: &[String]) -> ExitCode {
    let mut file: Option<&str> = None;
    let mut kind: Option<&str> = None;
    let mut out: Option<&str> = None;
    let mut i = 0;
    while i < args.len() {
        match args[i].as_str() {
            "--as" => {
                i += 1;
                match args.get(i) {
                    Some(k) => kind = Some(k),
                    None => {
                        eprintln!("error: --as requires obj|html|wasm");
                        return ExitCode::FAILURE;
                    }
                }
            }
            "-o" | "--output" => {
                i += 1;
                match args.get(i) {
                    Some(o) => out = Some(o),
                    None => {
                        eprintln!("error: -o requires a value");
                        return ExitCode::FAILURE;
                    }
                }
            }
            other if other.starts_with('-') => {
                eprintln!("error: unknown option '{other}'");
                return ExitCode::FAILURE;
            }
            p => {
                if file.is_some() {
                    eprintln!("error: expected one FILE");
                    return ExitCode::FAILURE;
                }
                file = Some(p);
            }
        }
        i += 1;
    }
    let (Some(path), Some(kind)) = (file, kind) else {
        eprintln!("Usage: zoltan export FILE --as obj|html|wasm [-o OUTPUT]");
        return ExitCode::FAILURE;
    };
    let source = match read_source(path) {
        Ok(s) => s,
        Err(m) => {
            eprintln!("{m}");
            return ExitCode::FAILURE;
        }
    };
    let info = match sketch::validate_sketch(&source) {
        Ok(info) => info,
        Err(e) => {
            eprintln!("error: {path}:{e}");
            return ExitCode::FAILURE;
        }
    };
    let program = match bytecode::compile(&source) {
        Ok(p) => p,
        Err(e) => {
            eprintln!("error: {path}:{e}");
            return ExitCode::FAILURE;
        }
    };
    match kind {
        "obj" => {
            let dest = out.unwrap_or("sketch.o").to_string();
            match write_minimal_elf(&program, &info.kernel.name, &dest) {
                Ok(()) => {
                    println!(
                        "ok: wrote {dest} (minimal ELF64, .jolt section, readable via lief.parse)"
                    );
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("error: {e}");
                    ExitCode::FAILURE
                }
            }
        }
        "html" => {
            let dest = out.unwrap_or("sketch.html").to_string();
            let js = match transpile_to_js(&source) {
                Ok(js) => js,
                Err(e) => {
                    eprintln!("error: {e}");
                    return ExitCode::FAILURE;
                }
            };
            let html = format!(
                "<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\">\
                 <title>{}</title></head>\n<body>\n<canvas id=\"c\" width=\"{}\" height=\"{}\"></canvas>\n\
                 <script>\nconst pixel={js};\nconst canvas=document.getElementById('c');\n\
                 const ctx=canvas.getContext('2d');\nconst img=ctx.createImageData(canvas.width,canvas.height);\n\
                 let t=0;\nfunction frame(){{\n for(let y=0;y<canvas.height;++y)for(let x=0;x<canvas.width;++x){{\n  \
                 const nx=x/(canvas.width-1),ny=y/(canvas.height-1);\n  \
                 const v=pixel(nx,ny,t,0.5,0.5);\n  const i=(y*canvas.width+x)*4;\n  \
                 img.data[i]=nx*255;img.data[i+1]=ny*255;img.data[i+2]=v*255;img.data[i+3]=255;\n }}\n \
                 ctx.putImageData(img,0,0);t+=1/30;requestAnimationFrame(frame);}}\nframe();\n</script>\n</body></html>\n",
                info.kernel.name, info.canvas_width, info.canvas_height
            );
            match fs::write(&dest, html) {
                Ok(()) => {
                    println!("ok: wrote {dest} (standalone HTML Canvas)");
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("error: cannot write '{dest}': {e}");
                    ExitCode::FAILURE
                }
            }
        }
        "wasm" => {
            let dest = out.unwrap_or("sketch.wat").to_string();
            // Stack-machine WAT mirroring the JBC1 program; assemble with
            // `wat2wasm` (wabt) when available. Full Emscripten bundling is a
            // separate step and needs a configured emsdk (see docs).
            let mut wat = "(module\n (func $pixel (param $x f32) (param $y f32) (param $t f32) (result f32)\n"
                .to_string();
            let mut offset = bytecode::HEADER_SIZE;
            while offset + 8 <= program.len() {
                let op = u32::from_le_bytes(program[offset..offset + 4].try_into().unwrap());
                let operand =
                    u32::from_le_bytes(program[offset + 4..offset + 8].try_into().unwrap());
                offset += 8;
                match op {
                    1 => wat.push_str(&format!("  f32.const {}\n", f32::from_bits(operand))),
                    2 => wat.push_str(&format!(
                        "  {}\n",
                        match operand {
                            0 => "local.get $x",
                            1 => "local.get $y",
                            2 => "local.get $t",
                            _ => "f32.const 0",
                        }
                    )),
                    3 => wat.push_str("  f32.add\n"),
                    4 => wat.push_str("  f32.sub\n"),
                    5 => wat.push_str("  f32.mul\n"),
                    6 => wat.push_str("  f32.div\n"),
                    7 => wat.push_str("  f32.min\n"),
                    8 => wat.push_str("  f32.max\n"),
                    9 => wat.push_str("  f32.abs\n"),
                    10 => wat.push_str("  f32.floor\n"),
                    11 => wat.push_str("  call $pow\n"),
                    12 => wat.push_str("  f32.sqrt\n"),
                    13 => wat.push_str("  f32.lt\n"),
                    14 => wat.push_str("  select\n"),
                    15 => {}
                    _ => {
                        eprintln!("error: WASM export: opcode {op} not lowered yet");
                        return ExitCode::FAILURE;
                    }
                }
            }
            wat.push_str(" )\n (func $pow (param f32 f32) (result f32) f32.const 0)\n");
            wat.push_str(" (export \"pixel\" (func $pixel)))\n");
            match fs::write(&dest, &wat) {
                Ok(()) => {
                    println!(
                        "ok: wrote {dest} (WAT; assemble with wat2wasm when wabt is installed)"
                    );
                    ExitCode::SUCCESS
                }
                Err(e) => {
                    eprintln!("error: cannot write '{dest}': {e}");
                    ExitCode::FAILURE
                }
            }
        }
        _ => {
            eprintln!("error: --as must be obj|html|wasm");
            ExitCode::FAILURE
        }
    }
}

fn cmd_stdlib(args: &[String]) -> ExitCode {
    match args.first().map(String::as_str) {
        Some("list") => {
            for s in stdlib::list() {
                println!("{} - {}", s.name, s.description);
            }
            ExitCode::SUCCESS
        }
        Some("show") => {
            let Some(name) = args.get(1) else {
                eprintln!("Usage: zoltan stdlib show NAME");
                return ExitCode::FAILURE;
            };
            match stdlib::show(name) {
                Some(s) => {
                    println!("{}", s.source);
                    ExitCode::SUCCESS
                }
                None => {
                    eprintln!("error: unknown snippet '{name}'");
                    ExitCode::FAILURE
                }
            }
        }
        _ => {
            eprintln!("Usage: zoltan stdlib list|show NAME");
            ExitCode::FAILURE
        }
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.is_empty() {
        eprint!("{}", usage());
        return ExitCode::FAILURE;
    }
    match args[0].as_str() {
        "compile" => cmd_compile(&args[1..]),
        "verify" => cmd_verify(&args[1..]),
        "new" => cmd_new(&args[1..]),
        "run" => cmd_run(&args[1..]),
        "export" => cmd_export(&args[1..]),
        "stdlib" => cmd_stdlib(&args[1..]),
        "help" => {
            if args.len() == 2 && args[1] == "compile" {
                print!("{0}", compile_help());
            } else {
                print!("{}", usage());
            }
            ExitCode::SUCCESS
        }
        "--version" | "-V" | "version" => {
            println!("zoltan {VERSION}");
            ExitCode::SUCCESS
        }
        "--help" | "-h" => {
            print!("{}", usage());
            ExitCode::SUCCESS
        }
        unknown => {
            eprintln!("error: unknown command '{unknown}'\n\n{}", usage());
            ExitCode::FAILURE
        }
    }
}
