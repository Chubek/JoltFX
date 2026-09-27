//! Zoltan compiler.
//!
//! `compile` validates a Joltscript kernel (see [`validate`]) and, with `-o`,
//! emits the real JBC1 bytecode the Joltscript VM and every backend execute
//! (see [`bytecode`]). `verify` only validates. Standard library only, so the
//! tool builds and runs offline.
//!
//! The bytecode emitter is a second implementation of the Glue Layer compiler
//! and must agree with it byte for byte. `scripts/check-bytecode-parity.sh`
//! compares the two over every bundled kernel, so a divergence fails the build
//! rather than producing a program the runtime rejects.

mod bytecode;
mod validate;

use std::env;
use std::fs;
use std::process::ExitCode;

use validate::validate_source;

const VERSION: &str = "0.2.0";

fn usage() -> String {
    format!(
        "zoltan {VERSION} - JoltScript compiler and live preview tool\n\
         \n\
         Usage: zoltan <command> [options]\n\
         \n\
         Commands:\n\
         \x20 compile FILE [-o OUTPUT] [--emit-metadata]  Validate a kernel and emit JBC1 bytecode\n\
         \x20 verify FILE                                 Validate a kernel without writing output\n\
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
     Validates a Joltscript kernel against the MVP subset (single\n\
     `(defkernel name [inputs...] body)` form, scalar f32 expressions) and,\n\
     with -o, writes the compiled JBC1 bytecode. Without -o nothing is written\n\
     and the kernel is only checked.\n\
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
    // Emit real JBC1 bytecode rather than staging the source: the output has to
    // be something a JoltFX runtime can execute. Re-validate the emitted
    // program before writing it, so a compiler bug cannot produce a file that
    // only fails later inside the VM.
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

fn main() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    if args.is_empty() {
        eprint!("{}", usage());
        return ExitCode::FAILURE;
    }
    match args[0].as_str() {
        "compile" => cmd_compile(&args[1..]),
        "verify" => cmd_verify(&args[1..]),
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
