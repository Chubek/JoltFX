//! Build the portable typed verifier/VM, with allocation supplied by Rust.
//! Source parsing and bytecode serialization remain independent Rust code.
use std::{env, path::PathBuf, process::Command};
fn run(command: &mut Command) {
    assert!(
        command.status().expect("run native toolchain").success(),
        "native typed VM build failed"
    );
}
fn main() {
    let root = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap()).join("..");
    let src = root.join("joltscript/layers/execution/src");
    let include = root.join("joltscript/layers/execution/include");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let python = env::var("PYTHON").unwrap_or_else(|_| "python3".into());
    run(Command::new(python)
        .arg(root.join("joltscript/stdlib/generate.py"))
        .arg(out.join("stdlib_catalog.h")));
    let pcre = out.join("pcre2");
    let mut configure = Command::new("cmake");
    configure
        .arg("-S")
        .arg(root.join("third_party/PCRE2"))
        .arg("-B")
        .arg(&pcre)
        .args([
            "-DBUILD_SHARED_LIBS=OFF",
            "-DBUILD_STATIC_LIBS=ON",
            "-DPCRE2_BUILD_TESTS=OFF",
            "-DPCRE2_BUILD_PCRE2GREP=OFF",
            "-DPCRE2_SUPPORT_JIT=OFF",
            "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
            "-DCMAKE_BUILD_TYPE=Release",
        ]);
    if let Ok(cc) = env::var("CC") {
        configure.arg(format!("-DCMAKE_C_COMPILER={cc}"));
    }
    if let Ok(toolchain) = env::var("CMAKE_TOOLCHAIN_FILE") {
        configure.arg(format!("-DCMAKE_TOOLCHAIN_FILE={toolchain}"));
    }
    run(&mut configure);
    run(Command::new("cmake").arg("--build").arg(&pcre).args([
        "--config",
        "Release",
        "--target",
        "pcre2-8-static",
        "--parallel",
        "4",
    ]));
    let target = env::var("TARGET").unwrap();
    let msvc = target.contains("msvc");
    let object = out.join(if msvc { "typed.obj" } else { "typed.o" });
    let cc = env::var("CC").unwrap_or_else(|_| if msvc { "cl" } else { "cc" }.into());
    let mut command = Command::new(cc);
    if msvc {
        command
            .args(["/nologo", "/std:c11", "/O2", "/DJOLT_TYPED_HOST", "/c"])
            .arg(format!("/I{}", include.display()))
            .arg(format!("/I{}", out.display()))
            .arg(format!("/I{}", pcre.display()))
            .arg("/DPCRE2_STATIC")
            .arg(src.join("typed_program.c"))
            .arg(format!("/Fo{}", object.display()));
    } else {
        command
            .args(["-std=c11", "-O2", "-fPIC", "-DJOLT_TYPED_HOST", "-c"])
            .arg("-I")
            .arg(&include)
            .arg("-I")
            .arg(&out)
            .arg("-I")
            .arg(&pcre)
            .arg("-DPCRE2_STATIC")
            .arg(src.join("typed_program.c"))
            .arg("-o")
            .arg(&object);
    }
    run(&mut command);
    let ar = env::var("AR").unwrap_or_else(|_| if msvc { "lib" } else { "ar" }.into());
    let mut command = Command::new(ar);
    if msvc {
        command
            .arg("/nologo")
            .arg(format!("/OUT:{}", out.join("jolt_typed.lib").display()))
            .arg(&object);
    } else {
        command
            .arg("crs")
            .arg(out.join("libjolt_typed.a"))
            .arg(&object);
    }
    run(&mut command);
    println!("cargo:rustc-link-search=native={}", out.display());
    println!("cargo:rustc-link-lib=static=jolt_typed");
    println!("cargo:rustc-link-search=native={}", pcre.display());
    println!(
        "cargo:rustc-link-search=native={}",
        pcre.join("Release").display()
    );
    println!(
        "cargo:rustc-link-lib=static={}",
        if msvc { "pcre2-8-static" } else { "pcre2-8" }
    );
    if !msvc {
        println!("cargo:rustc-link-lib=m");
    }
    for file in [
        src.join("typed_program.c"),
        src.join("stdlib_runtime.inc"),
        src.join("typed_internal.h"),
        src.join("bytecode_internal.h"),
        include.join("joltscript/value.h"),
        include.join("joltscript/vm.h"),
    ] {
        println!("cargo:rerun-if-changed={}", file.display());
    }
    println!(
        "cargo:rerun-if-changed={}",
        root.join("joltscript/stdlib").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("third_party/PCRE2/src").display()
    );
    println!(
        "cargo:rerun-if-changed={}",
        root.join("third_party/PCRE2/CMakeLists.txt").display()
    );
    println!("cargo:rerun-if-env-changed=PYTHON");
    println!("cargo:rerun-if-env-changed=CMAKE_TOOLCHAIN_FILE");
    println!("cargo:rerun-if-env-changed=CC");
    println!("cargo:rerun-if-env-changed=AR");
}
