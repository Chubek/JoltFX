#!/usr/bin/env python3
"""End-to-end Phase 1 C/Rust byte parity, typed REPL values and rejection tests."""
import argparse
import pathlib
import subprocess
import tempfile

CASES = [
    ("(defkernel k [] 18446744073709551615u64)", "18446744073709551615"),
    ("(defkernel k [] (+ 9007199254740993i64 2i64))", "9007199254740995"),
    ("(defkernel k [] (i32 (+ 255u8 1u8)))", "0"),
    ("(defkernel k [] (rgba 65535u16 4294967295u32 -2147483648i32 -9223372036854775808i64))", "65535 4294967295 -2147483648 -9223372036854775808"),
    ("(defkernel k [] (- 16777217f32 16777216f32))", "0"),
    ("(defkernel k [] (- 16777217f64 16777216f64))", "1"),
    ("(defkernel k [] (if true \"héllo😀\" \"no\"))", "héllo😀"),
    ("(defkernel k [] \\u{1F600})", "\\u{1F600}"),
    ("(defkernel k [] (= \"a\\0b\" \"a\\0b\"))", "true"),
    ("(defkernel k [] (var n 0) (for [i 0 10] (set! n (+ n i))) n)", "45"),
    ("(defkernel k [] (var n 0) (for [i 10 0 -2] (set! n (+ n i))) n)", "30"),
    ("(defkernel k [] (var n 0) (while (< n 5) (set! n (+ n 1))) n)", "5"),
    ("(defkernel k [] (var n 0) (loop (set! n (+ n 1)) (when (= n 3) (break))) n)", "3"),
    ("(defkernel k [] (var n 0) (for [i 0 5] (when (= i 2) (continue)) (set! n (+ n i))) n)", "8"),
    ("(defn fact [(n i64)] -> i64 (if (<= n 1i64) 1i64 (* n (fact (- n 1i64))))) (defkernel k [] (fact 10i64))", "3628800"),
    ("(defkernel k [] (let n 7) (let f (fn [(x i32)] (+ n x))) (let [n 99] (f 3)))", "10"),
    ("(defkernel k [] (let make (fn [(x i32)] (fn [(y i32)] (+ x y)))) (let f (make 7)) (f 5))", "12"),
    ("(defn first [] (second)) (defn second [] 7) (defkernel k [] (first))", "7"),
    ("(defkernel k [] (let (x i32) 7) (let (y f64) 3.5) (+ (f64 x) y))", "10.5"),
    ("(defkernel k [] (bitwise-xor (shl 1u64 63) 18446744073709551615u64))", "9223372036854775807"),
]
INVALID = [
    "(defkernel k [] 256u8)", "(defkernel k [] 18446744073709551616u64)",
    "(defkernel k [] (if true 1 \"x\"))", "(defkernel k [] (break))",
    "(defkernel k [] (for [i 0.0 4.0] ()))", "(defkernel k [] \\u{D800})",
    "(defkernel k [] (let (x u8) 256) x)", "(defkernel k [] ((fn [(x i32)] x) \"x\"))",
    "(defkernel k [] (loop ((fn [] (break)))))", "(defkernel k [] (+ 1i64 1u64))",
    "(defn f [] (f)) (defkernel k [] (f))", "(defn f [] x) (defkernel k [] (let x 7) (f))",
]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--joltc", required=True)
    parser.add_argument("--jolti", required=True)
    parser.add_argument("--zoltan", required=True)
    args = parser.parse_args()
    metadata = ";; @kernel k\n;; @category conformance\n;; @description Phase 1\n;; @complexity Low\n;; @gpu No\n;; @since 0.8.0\n"
    with tempfile.TemporaryDirectory(prefix="phase1-") as tmp:
        work = pathlib.Path(tmp)
        source, c, rust = (work / n for n in ["case.jolt", "c.jbc", "rust.jbc"])
        for text, expected in CASES:
            source.write_text(metadata + text)
            for command in [(args.joltc, str(source), "-o", str(c)), (args.zoltan, "compile", str(source), "-o", str(rust))]:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                assert result.returncode == 0, (text, result.stderr)
            assert c.read_bytes() == rust.read_bytes(), text
            result = subprocess.run([args.jolti, "-e", text], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and result.stdout.rstrip("\n") == expected, (text, result.stdout, result.stderr)
        for text in INVALID:
            source.write_text(metadata + text)
            for command in [(args.joltc, str(source), "--check"), (args.zoltan, "compile", str(source), "-o", str(rust))]:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                assert result.returncode != 0 and result.stderr, text
        source.write_text(metadata + CASES[0][0].replace("[]", "[x y t]"))
        for kind in ["html", "wasm"]:
            destination = work / f"existing.{kind}"
            destination.write_text("previous artifact")
            result = subprocess.run([args.zoltan, "export", str(source), "--as", kind, "-o", str(destination)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode != 0 and "version-2" in result.stderr, result.stderr
            assert destination.read_text() == "previous artifact"
        subprocess.run([args.zoltan, "compile", str(source), "-o", str(rust)], check=True, capture_output=True)
        obj = work / "typed.o"
        subprocess.run([args.zoltan, "export", str(source), "--as", "obj", "-o", str(obj)], check=True, capture_output=True)
        assert obj.read_bytes().startswith(b"\x7fELF") and rust.read_bytes() in obj.read_bytes()
    print(f"{len(CASES)} Phase 1 parity/exact execution cases and {len(INVALID)} C/Rust rejection cases passed")

if __name__ == "__main__":
    main()
