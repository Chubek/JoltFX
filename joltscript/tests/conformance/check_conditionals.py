#!/usr/bin/env python3
"""Compile C/Rust parity cases and execute Zoltan's JS and optional Wasm exports.

python3 joltscript/tests/conformance/check_conditionals.py \
    --joltc build/joltscript/tools/joltc/joltc --zoltan zoltan/target/debug/zoltan
Add --wasm with the Python wasmtime package installed for binary execution.
"""

import argparse
import json
import pathlib
import subprocess
import tempfile


CASES = [
    ("(if x 7 (/ 1 0))", 7),
    ("(if 0 (/ 1 0) (+ x 2))", 5),
    ("(+ 10 (if x (if 0 2 3) 4))", 13),
    ("(do (var a 1) (if x (set! a 7) (set! a 9)) a)", 7),
    ("(when x (let a 4) (+ a x))", 7),
    ("(unless x (/ 1 0))", 0),
    ("(when 0 (/ 1 0))", 0),
    ("(unless 0 (let a 2) (+ a x))", 5),
    ("(cond [(< x 0) (/ 1 0)] [(= x 3) 8] [else (/ 1 0)])", 8),
    ("(cond [0 10] [else x])", 3),
    ("(cond [0 (/ 1 0)])", 0),
    ("(do (let a 2) (if x (let a 10) (let a 20)) a)", 2),
    ("(do (var a 0) (cond [(set! a 1) a] [(set! a 2) a]) a)", 1),
    ("(rgba (if x 7 8) (when y 2) (unless t 3) (cond [1 4]))", 7),
    ("(if 1 (/ 1 0) 7)", None),
    ("(if (/ 1 0) 7 8)", None),
    ("(select 1 7 (/ 1 0))", None),
    ("(if (not (= x 3)) (/ 1 0) (select 0 8 9))", 9),
    ("(do (var a 0) (if (set! a (+ a 1)) a 0) a)", 1),
]
INVALID = [
    "(if 1 2)", "(if 1 2 3 4)", "(when 1)", "(unless)", "(cond)",
    "(cond [else 1] [1 2])", "(cond [1])", "(cond [1 2 3])",
    "(do (if 1 (let a 2) 3) a)", "(if (let c 1) c 0)",
    "(if 1 (let a 2) a)", "(if 1 2 unknown)",
    "(cond [(let c 1) c])", "(do (when 1 (let a 2)) a)",
]


def source(body):
    return (";; @kernel conditional\n;; @category conformance\n"
            ";; @description Conditional export test.\n;; @complexity Low\n"
            ";; @gpu Yes\n;; @since 0.7.0\n"
            f"(defkernel conditional [x y t] {body})\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--joltc", required=True)
    parser.add_argument("--zoltan", required=True)
    parser.add_argument("--node", default="node")
    parser.add_argument("--wasm", action="store_true")
    args = parser.parse_args()
    if args.wasm:
        import wasmtime
        engine = wasmtime.Engine()

    def run(*command):
        return subprocess.run(command, check=True, capture_output=True, text=True, timeout=30)

    with tempfile.TemporaryDirectory(prefix="joltscript-branches-") as directory:
        work = pathlib.Path(directory)
        src, c, rust = (work / name for name in ("case.jolt", "c.jbc", "rust.jbc"))
        for body, expected in CASES:
            src.write_text(source(body))
            run(args.joltc, str(src), "-o", str(c))
            run(args.zoltan, "compile", str(src), "-o", str(rust))
            assert c.read_bytes() == rust.read_bytes(), body
            html, wat = work / "case.html", work / "case.wat"
            run(args.zoltan, "export", str(src), "--as", "html", "-o", str(html))
            js = html.read_text().split("const pixel=", 1)[1].split("\nconst canvas", 1)[0]
            harness = (f"const pixel={js}\nlet result=null;"
                       "try {result=pixel(3,0,0,0,0);} catch(e) {"
                       "if(!String(e).includes('non-finite'))throw e;}"
                       "console.log(JSON.stringify(result));")
            actual = json.loads(run(args.node, "-e", harness).stdout)
            assert actual == expected, (body, actual, expected)
            run(args.zoltan, "export", str(src), "--as", "wasm", "-o", str(wat))
            if args.wasm:
                module = wasmtime.Module(engine, wasmtime.wat2wasm(wat.read_text()))
                store = wasmtime.Store(engine)
                instance = wasmtime.Instance(store, module, [])
                try:
                    actual = instance.exports(store)["pixel"](store, 3.0, 0.0, 0.0)
                except wasmtime.Trap:
                    actual = None
                assert actual == expected, (body, actual, expected)
        for body in INVALID:
            src.write_text(source(body))
            for command in [(args.joltc, str(src), "--check"),
                            (args.zoltan, "compile", str(src), "-o", str(rust))]:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                assert result.returncode != 0 and result.stderr, (body, command)
        # Wasm has no built-in pow instruction. Do not resurrect the old
        # placeholder function that silently returned zero for every input.
        src.write_text(source("(if x (pow x 2) 0)"))
        wat = work / "unsupported.wat"
        wat.write_text("previous artifact")
        result = subprocess.run([args.zoltan, "export", str(src), "--as", "wasm", "-o", str(wat)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode != 0 and "not lowered" in result.stderr
        assert wat.read_text() == "previous artifact"
    print(f"{len(CASES)} conditional parity/JS{'/Wasm' if args.wasm else ''} executions and "
          f"{len(INVALID)} C/Rust rejection cases passed")


if __name__ == "__main__":
    main()
