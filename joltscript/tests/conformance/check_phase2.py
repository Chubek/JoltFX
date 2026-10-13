#!/usr/bin/env python3
"""Exercise every shipped intrinsic through C/Rust compilation and the native VM."""
import argparse
import math
import pathlib
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = []


def case(module, name, args, expected):
    CASES.append((module, name, f"({name} {args})", str(expected)))


for name in ["sin", "cos", "tan", "asin", "acos", "atan", "exp", "log", "log10", "log2", "exp2", "sqrt", "cbrt", "floor", "ceil", "trunc"]:
    fn = getattr(math, name, lambda x: 2 ** x)
    case("math", name, "0.5", fn(0.5))
for name, args, want in [
    ("atan2", "1 1", math.pi / 4), ("pow", "2 3", 8), ("hypot", "3 4", 5),
    ("round", "2.5", 3), ("abs", "-3", 3), ("sign", "-3", -1),
    ("min", "3 2", 2), ("max", "3 2", 3), ("mod", "-1 3", 2),
    ("clamp", "2 0 1", 1), ("lerp", "10 20 0.25", 12.5),
    ("smoothstep", "0 1 0.5", .5), ("map-range", "5 0 10 0 100", 50),
    ("deg->rad", "180", math.pi), ("rad->deg", "3.141592653589793", 180),
    ("dot", "[1 2] [3 4]", 11), ("length", "[3 4]", 5),
    ("normalize", "[3 4]", "[0.6 0.8]"), ("cross", "[1 0 0] [0 1 0]", "[0 0 1]"),
    ("mat2-mul", "[1 2 3 4] [5 6 7 8]", "[19 22 43 50]"), ("mat2-det", "[1 2 3 4]", -2),
]:
    case("math", name, args, want)
for name, want in [("PI", math.pi), ("TWO-PI", 2*math.pi), ("HALF-PI", math.pi/2), ("E", math.e), ("SQRT2", math.sqrt(2)), ("SQRT1_2", math.sqrt(.5))]:
    CASES.append(("math", name, name, str(want)))
CASES.append(("math", "random-seed", "(let rng (random-seed 42u32)) (rng)", str(1083814273 / 2**32)))
for module in ["math", "time"]:
    for family in (["quad", "cubic"] if module == "math" else ["quad", "cubic", "quart", "quint", "sine", "expo", "circ", "elastic", "back", "bounce"]):
        for mode in ["in", "out", "in-out"]:
            name = f"ease-{mode}-{family}"
            for point in [0, 1]:
                case(module, name, str(point), point)
for name, args, want in [
    ("fps->frame-time", "50", .02), ("frame-time->fps", ".02", 50),
    ("seconds->frames", "2 30", 60), ("frames->seconds", "60 30", 2),
    ("keyframe-value", "[[10 100] [0 0] [5 30]] 7.5", 65),
    ("ease-linear", ".25", .25), ("linear-curve", ".25", .25),
    ("bezier-curve", ".5 0 1 2 3", 1.5), ("catmull-rom-curve", ".5 0 1 2 3", 1.5),
    ("animate", "2 1 (fn [x] (* x x))", .25), ("ping-pong", "1.25", .75),
    ("loop-time", "-1 3", 2), ("repeat-count", "7 3", 2),
]:
    case("time", name, args, want)
for dim in [2, 3]:
    p = "[1 2]" if dim == 2 else "[1 2 3]"
    case("geometry", f"point{dim}d", "1 2" if dim == 2 else "1 2 3", p)
    for index, axis in enumerate("xyz"[:dim]):
        case("geometry", f"point{dim}d-{axis}", p, index+1)
    for op, want in [("add", "[2 4]" if dim == 2 else "[2 4 6]"), ("sub", "[0 0]" if dim == 2 else "[0 0 0]"), ("dist", 0)]:
        case("geometry", f"point{dim}d-{op}", f"{p} {p}", want)
    case("geometry", f"point{dim}d-scale", f"{p} 2", "[2 4]" if dim == 2 else "[2 4 6]")
    case("geometry", f"point{dim}d-lerp", f"{p} {p} .5", p)
for name, args, want in [
    ("vec2", "1 2", "[1 2]"), ("vec3", "1 2 3", "[1 2 3]"), ("vec4", "1 2 3 4", "[1 2 3 4]"),
    ("vec-dot", "[1 2 3] [4 5 6]", 32), ("vec-length", "[3 4]", 5),
    ("vec-normalize", "[0 0]", "[0 0]"), ("vec-cross", "[1 0 0] [0 1 0]", "[0 0 1]"),
    ("vec-lerp", "[0 2] [2 4] .5", "[1 3]"),
    ("translate2d", "[1 2] 3 4", "[4 6]"), ("scale2d", "[1 2] 3 4", "[3 8]"),
    ("rotate2d", "[1 0] 1.5707963267948966", "[0 1]"),
    ("translate3d", "[1 2 3] 3 4 5", "[4 6 8]"), ("scale3d", "[1 2 3] 3 4 5", "[3 8 15]"),
    ("rotate3d-x", "[0 1 0] 1.5707963267948966", "[0 0 1]"),
    ("rotate3d-y", "[0 0 1] 1.5707963267948966", "[1 0 0]"),
    ("rotate3d-z", "[1 0 0] 1.5707963267948966", "[0 1 0]"),
    ("mat-mul", "[1 2 3 4] [5 6 7 8] 2", "[19 22 43 50]"),
    ("mat-transpose", "[1 2 3 4] 2", "[1 3 2 4]"),
    ("quat", "1 2 3 4", "[1 2 3 4]"), ("quat-identity", "", "[0 0 0 1]"),
    ("quat-mul", "[1 2 3 4] [0 0 0 1]", "[1 2 3 4]"), ("quat-normalize", "[0 0 0 2]", "[0 0 0 1]"),
    ("quat-from-axis-angle", "[0 0 1] 0", "[0 0 0 1]"),
    ("quat-rotate", "[0 0 0 1] [1 2 3]", "[1 2 3]"),
    ("catmull-rom-spline", "[0 0] [1 1] [2 2] [3 3] .5", "[1.5 1.5]"),
    ("bezier-spline", "[0 0] [1 1] [2 2] [3 3] .5", "[1.5 1.5]"),
    ("bbox2d", "[[3 1] [1 4]]", "[1 1 3 4]"), ("bbox3d", "[[3 1 5] [1 4 2]]", "[1 1 2 3 4 5]"),
    ("point-in-bbox2d", "[2 2] [1 1 3 4]", "true"), ("point-in-bbox3d", "[2 2 6] [1 1 2 3 4 5]", "false"),
]:
    case("geometry", name, args, want)
for name, args, want in [
    ("rgb->hsv", "1 0 0", "[0 1 1]"), ("hsv->rgb", "0 1 1", "[1 0 0]"),
    ("rgb->hsl", "1 0 0", "[0 1 0.5]"), ("hsl->rgb", "0 1 .5", "[1 0 0]"),
    ("brightness", ".2 .3 .4 2", "[.4 .6 .8 1]"), ("contrast", ".2 .3 .4 0", "[.5 .5 .5 1]"),
    ("saturation", ".2 .3 .4 1", "[.2 .3 .4 1]"), ("invert", ".2 .3 .4 .5", "[.8 .7 .6 .5]"),
    ("grayscale", "1 1 1", "[1 1 1 1]"), ("sepia", "1 1 1", "[1 1 .937 1]"),
    ("blend-normal", ".2 .4", .2), ("blend-multiply", ".2 .4", .08), ("blend-screen", ".2 .4", .52),
    ("blend-overlay", ".2 .4", .16), ("blend-darken", ".2 .4", .2), ("blend-lighten", ".2 .4", .4),
    ("blend-difference", ".2 .4", .2), ("alpha-composite", "1 0 0 .5 0 0 1 1", "[.5 0 .5 1]"),
    ("color-temperature", "1 1 1 6600", "[1 1 1]"), ("gamma-correct", ".5 .5 .5 2", "[.25 .25 .25 1]"),
    ("gamma-decode", "1 0 1", "[1 0 1 1]"), ("gamma-encode", "1 0 1", "[1 0 1 1]"),
    ("convolve", "[1 4 7] [1 2 1]", 4), ("box-filter", "[1 4 7]", 4),
    ("gaussian-weight", "0 1", 1/math.sqrt(2*math.pi)),
]:
    case("graphics", name, args, want)
for name, args, want in [
    ("list", "1 2", "(list 1 2)"), ("vector", "1 2", "[1 2]"), ("hash-map", '"a" 1 "a" 2', '(hash-map a 2)'),
    ("set", "1 2 1", "(set 1 2)"), ("length", "[1 2]", 2), ("empty?", "[]", "true"), ("not-empty?", "[1]", "true"),
    ("nth", "[1 2] 1", 2), ("first", "[1 2]", 1), ("rest", "[1 2]", "[2]"),
    ("last", "[1 2]", 2), ("butlast", "[1 2]", "[1]"), ("slice", "[1 2 3] 1 2", "[2]"),
    ("concat", "[1] [2 3]", "[1 2 3]"), ("take", "2 [1 2 3]", "[1 2]"), ("drop", "2 [1 2 3]", "[3]"),
    ("take-while", "(fn [x] (< x 3)) [1 2 3 1]", "[1 2]"), ("drop-while", "(fn [x] (< x 3)) [1 2 3 1]", "[3 1]"),
    ("partition", "2 [1 2 3]", "[[1 2]]"), ("partition-all", "2 [1 2 3]", "[[1 2] [3]]"),
    ("interpose", "0 [1 2 3]", "[1 0 2 0 3]"), ("interleave", "[1 3] [2 4 5]", "[1 2 3 4 5]"),
    ("zipmap", '["a" "b"] [1 2]', '(hash-map a 1 b 2)'), ("frequencies", "[1 1 2]", "(hash-map 1 2 2 1)"),
    ("group-by", "(fn [(x value)] (> x 1)) [1 2 3]", "(hash-map false [1] true [2 3])"),
    ("vec-get", "[1 2] 0", 1), ("vec-set", "[1 2] 0 3", "[3 2]"), ("vec-push", "[1 2] 3", "[1 2 3]"),
    ("vec-pop", "[1 2]", "[1]"), ("vec-peek", "[1 2]", 2),
    ("get", '(hash-map "a" 1) "a"', 1), ("assoc", '(hash-map "a" 1) "a" 2', '(hash-map a 2)'),
    ("dissoc", '(hash-map "a" 1 "b" 2) "a"', '(hash-map b 2)'),
    ("map-get", '(hash-map "a" 1) "b" 3', 3), ("map-get-in", '(hash-map "a" (hash-map "b" 2)) ["a" "b"]', 2),
    ("map-keys", '(hash-map "a" 1 "b" 2)', '[a b]'), ("map-vals", '(hash-map "a" 1 "b" 2)', '[1 2]'),
    ("map-merge", '(hash-map "a" 1) (hash-map "a" 2 "b" 3)', '(hash-map a 2 b 3)'),
    ("map-invert", '(hash-map "a" 1 "b" 2)', '(hash-map 1 a 2 b)'),
    ("set-union", "[1 2] [2 3]", "(set 1 2 3)"), ("set-intersection", "[1 2] [2 3]", "(set 2)"),
    ("set-difference", "[1 2] [2 3]", "(set 1)"), ("contains?", "[1 2] 2", "true"),
    ("map", "+ [1 2] [3 4]", "[4 6]"), ("map-indexed", "+ [10 20]", "[10 21]"),
    ("filter", "(fn [x] (> x 1)) [1 2 3]", "[2 3]"), ("keep", "(fn [(x value)] x) [1 () 3]", "[1 3]"),
    ("remove", "(fn [x] (> x 1)) [1 2 3]", "[1]"), ("reduce", "+ 0 [1 2 3]", 6),
    ("reduce-kv", '(fn [(n value) (key value) (v value)] (+ n v)) 0 (hash-map "a" 1 "b" 2)', 3),
    ("apply", "+ [1 2]", 3), ("apply-kv", "+ (hash-map 1 2)", 3),
    ("some", "(fn [x] (> x 2)) [1 2 3]", "true"), ("every?", "(fn [x] (> x 2)) [1 2 3]", "false"),
    ("sort", "[3 1 2] <", "[1 2 3]"),
]:
    case("collections", name, args, want)
for name, expression, want in [
    ("range-seq", "(take 4 (range-seq 5 -1 -2))", "[5 3 1]"),
    ("iterate", "(take 4 (iterate (fn [(x value)] (+ x 1)) 0))", "[0 1 2 3]"),
    ("repeatedly", "(take 3 (repeatedly (fn [] 7)))", "[7 7 7]"),
    ("cycle", "(take 5 (cycle [1 2]))", "[1 2 1 2 1]"),
    ("lazy-seq", "(take 3 (lazy-seq (fn [(state value)] [7 state])))", "[7 7 7]"),
]:
    CASES.append(("collections", name, expression, want))
for name, args, want in [
    ("str-length", '"a😀é"', 3), ("str-empty?", '""', "true"), ("str-not-empty?", '"x"', "true"),
    ("str-trim", '"  x \\n"', "x"), ("str-trim-left", '"  x "', "x "), ("str-trim-right", '" x  "', " x"),
    ("str-upper", '"abcé"', "ABCé"), ("str-lower", '"ABCé"', "abcé"), ("str-capitalize", '"hELLO"', "Hello"),
    ("str-replace", '"aaa" "a" "b"', "baa"), ("str-replace-all", '"aaa" "a" "aa"', "aaaaaa"),
    ("str-split", '"a,b,c" ","', "[a b c]"), ("str-join", '"," ["a" "b"]', "a,b"),
    ("str-concat", '"a" "b"', "ab"), ("str-contains?", '"abc" "bc"', "true"),
    ("str-starts-with?", '"abc" "ab"', "true"), ("str-ends-with?", '"abc" "bc"', "true"),
    ("parse-int", '"9007199254740993"', "9007199254740993"), ("parse-float", '"1.25e2"', 125),
    ("parse-bool", '"TRUE"', "true"), ("format", '"{}:{1}:{{ok}}" 18446744073709551615u64 "é"', "18446744073709551615:é:{ok}"),
    ("format-int", "18446744073709551615u64", "18446744073709551615"), ("format-float", "1.25 2", "1.25"),
    ("char-at", '"a😀é" 1', "\\u{1F600}"), ("substring", '"a😀é" 1 3', "😀é"),
    ("str-reverse", '"a😀é"', "é😀a"), ("str-repeat", '"ab" 3', "ababab"),
    ("str-pad-left", '"x" 3 "é"', "ééx"), ("str-pad-right", '"x" 3 "é"', "xéé"),
    ("whitespace?", "\\u{2003}", "true"), ("str-compare", '"a" "b"', -1),
    ("str-equals?", '"a" "a"', "true"), ("str-less?", '"a" "b"', "true"), ("str-greater?", '"b" "a"', "true"),
    ("regex-match?", '"[a-z]+" "123abc"', "true"), ("regex-find", '"[a-z]+" "123abc456"', "abc"),
    ("regex-replace", '"([0-9]+)" "a12b3" "<$1>"', "a<12>b<3>"),
]:
    case("strings", name, args, want)


def equivalent(actual, expected):
    if actual == expected:
        return True
    # Only floating results use tolerances; exact integer/string cases stay exact.
    if re.fullmatch(r"[-+0-9.eE\[\] ]+", expected):
        a, b = actual.replace("[", " [ ").replace("]", " ] ").split(), expected.replace("[", " [ ").replace("]", " ] ").split()
        if len(a) != len(b):
            return False
        for x, y in zip(a, b):
            if x == y:
                continue
            try:
                if not math.isclose(float(x), float(y), rel_tol=1e-9, abs_tol=1e-9):
                    return False
            except ValueError:
                return False
        return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ["joltc", "jolti", "zoltan"]:
        parser.add_argument("--" + arg, required=True)
    args = parser.parse_args()
    covered = {(m, n) for m, n, _, _ in CASES}
    declarations = {(path.stem, name) for path in (ROOT / "stdlib").glob("*.jolt") for name in re.findall(r"\(intrinsic ([^ ]+)", path.read_text())}
    assert covered == declarations, ("uncovered", declarations - covered, "unknown", covered - declarations)
    with tempfile.TemporaryDirectory(prefix="phase2-") as tmp:
        work = pathlib.Path(tmp)
        source, c, rust = (work / n for n in ["case.jolt", "c.jbc", "rust.jbc"])
        def parity(path):
            for command in [(args.joltc, str(path), "-o", str(c)), (args.zoltan, "compile", str(path), "-o", str(rust))]:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                assert result.returncode == 0, (str(path), path.read_text(), result.stderr)
            assert c.read_bytes() == rust.read_bytes(), path.read_text()
        for path in (ROOT / "stdlib").glob("*.jolt"):
            parity(path)
        for module, name, expression, expected in CASES:
            text = f"(import jolt.{module}) (defkernel k [] {expression})"
            source.write_text(";; @kernel k\n;; @category conformance\n;; @description Phase 2\n;; @complexity Low\n;; @gpu No\n;; @since 0.9.0\n" + text)
            parity(source)
            result = subprocess.run([args.jolti, "-e", text], capture_output=True, text=True, timeout=30)
            assert result.returncode == 0 and equivalent(result.stdout.removesuffix("\n"), expected), (module, name, expected, result.stdout, result.stderr)
        invalid = [
            "(import jolt.unknown) (defkernel k [] 1)",
            "(import jolt.math (missing)) (defkernel k [] 1)",
            "(import jolt.math (sin)) (defkernel k [] (cos 1))",
            "(import jolt.math) (defkernel k [] (sin))",
            '(import jolt.math) (defkernel k [] (sin "x"))',
            "(import jolt.math) (import jolt.collections) (defkernel k [] (length [1]))",
            "(import jolt.collections) (defkernel k [] (map 1 [2]))",
            "(module jolt.math) (intrinsic sin [string] -> f64) (export *)",
            "(module jolt.math) (intrinsic sin [number] -> string) (export *)",
            "(module jolt.math) (defn f [] missing)",
            "(module jolt.math) (module jolt.strings)",
            "(import jolt.collections) (defkernel k [] (let f +) (f " + " ".join(["1"]*33) + "))",
        ]
        for text in invalid:
            source.write_text(";; @kernel k\n;; @category conformance\n;; @description Phase 2\n;; @complexity Low\n;; @gpu No\n;; @since 0.9.0\n" + text)
            for command in [(args.joltc, str(source), "--check"), (args.zoltan, "compile", str(source), "-o", str(rust))]:
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                assert result.returncode != 0 and result.stderr, text
    print(f"{len(declarations)} intrinsic exports covered; {len(CASES)} execution/parity cases, {len(invalid)} rejection cases and six module parity checks passed")


if __name__ == "__main__":
    main()
