#!/usr/bin/env python3
"""Central audit for JoltFX image kernels.

Checks, per kernel source:
  1. compiles under the shipping compiler driver (joltc --check)
  2. complete metadata, no blank fields
  3. 3-6 params, integer params have whole-number defaults,
     defaults inside their declared range
  4. every param actually used in the kernel body
  5. body is not a passthrough stub
  6. category directory matches the @category tag
  7. a .test.jolt exists
  8. registered in kernels/CMakeLists.txt
  9. registered in the tests/unit verify list

Usage:  python3 scripts/audit-kernels.py [--quick]
        --quick  skips checks that need a build (registration parity only)
"""
import csv, io, os, re, subprocess, sys, glob

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
JC = os.path.join(ROOT, "build/joltscript/tools/joltc/joltc")
LIB = os.path.join(ROOT, "kernels/common/image.jolt")
CSV = os.path.join(ROOT, "kernels/JoltFX-Kernels.csv")

CATEGORY_DIR = {
    "COLOR": "color", "TRANSFORM": "transform", "BLUR_SHARPEN": "blur_sharpen",
    "DISTORTION": "distortion", "GENERATIVE": "generative", "COMPOSITING": "compositing",
    "NOISE": "noise", "FRACTAL": "fractal", "UTILITY": "utility", "LIGHT": "light",
    "KEYING": "keying", "GEOMETRY": "geometry", "MASK_MATTE": "mask_matte",
    "NLE": "nle", "TIME": "time", "TRANSITION": "transition",
    "TEMPOrAL": "temporal", "TEMPORAL": "temporal",
    "DATA_ANALYSIS": "data_analysis", "TEXT": "text", "TEXT_OVERLAY": "text_overlay",
    "VIDEO_EFFECTS": "video_effects", "VIDEO_COMPOSITING": "compositing",
    "STYLIZE": "stylize", "COLOR_GRADING": "color_grading",
    "COLOR_CALIBRATION": "color_calibration", "PARTICLE": "particle",
    "AUDIO_REACTIVE": "audio_reactive", "3D": "3d", "TRACKING": "tracking",
}

REQUIRED_META = ["@kernel", "@category", "@description", "@complexity", "@gpu", "@since"]

# The profile's only output is an image. These output types cannot be produced,
# so the kernel cannot be implemented here at all.
UNREPRESENTABLE = {"AudioBuffer", "Mesh", "ParticleSystem", "Path", "Point[]", "Vec2[]"}

STUB_PATTERNS = [
    r"\(defkernel \w+ \[x y c\] \(pixel x y c\)\)",
    r"\(defkernel \w+ \[x y c\] \(if \(= c 3\) \(pixel x y 3\) \(pixel x y c\)\)\)",
    r"\(defkernel \w+ \[x y c\] \(if \(= c 3\) 1 \(pixel x y c\)\)\)",
    r"\(defkernel \w+ \[x y c\] \(if \(= c 3\) 1 0\)\)",
    r"\(defkernel \w+ \[x y c\] \(if \(= c 3\) \(pixel x y 3\) 0\)\)",
]

# The legacy JBC1 catalogue embedded by kernels/src/effects.c. It is a separate
# execution path from the image profile and is deliberately not registered in
# IMAGE_KERNELS, so the image-profile conventions below do not apply to it.
LEGACY_CATALOG = {
    "brightness", "contrast", "exposure", "gamma", "grayscale", "invert",
    "opacity", "posterize", "saturation", "sepia", "threshold", "tint",
}


def kernel_body(text):
    i = text.find("(defkernel")
    if i < 0:
        return ""
    depth, j = 0, i
    while j < len(text):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                break
        j += 1
    return re.sub(r"\s+", " ", text[i:j + 1]).strip()


def registered_list(path, marker):
    s = open(path).read()
    i = s.index(marker)
    o = s.index("(", i)
    c = s.index("\n)\n", o)
    return [t for t in s[o + 1:c].split() if "/" in t]


def main():
    quick = "--quick" in sys.argv
    reg = set(registered_list(os.path.join(ROOT, "kernels/CMakeLists.txt"),
                               "set(IMAGE_KERNELS"))
    ver = set(registered_list(os.path.join(ROOT, "tests/unit/CMakeLists.txt"),
                              "foreach(image_kernel IN ITEMS"))

    rows = list(csv.DictReader(io.StringIO(open(CSV, encoding="utf-8-sig").read())))
    by_name = {r["Kernel Name"]: r for r in rows}

    files = sorted(f for f in glob.glob(os.path.join(ROOT, "kernels/**/*.jolt"), recursive=True)
                   if "/common/" not in f)

    issues = []
    stub_count = 0
    for path in files:
        rel = os.path.relpath(path, ROOT)
        text = open(path).read()
        name = os.path.basename(path)[:-5]
        folder = os.path.basename(os.path.dirname(path))

        legacy = name in LEGACY_CATALOG

        # 1. compiles. The legacy JBC1 catalogue uses the scalar kernel signature
        # and is compiled without --image-library, so it is checked differently.
        if not quick and os.path.exists(JC):
            if legacy:
                r = subprocess.run([JC, "--check", path], capture_output=True, text=True)
                ok = r.returncode == 0
            else:
                r = subprocess.run([JC, "--check", "--image-library", LIB, path],
                                   capture_output=True, text=True)
                ok = "OK (CPU image)" in r.stdout
            if not ok:
                issues.append(f"{rel}: does not compile: {r.stdout.strip()[:90]}")

        # 5. stub
        kb = kernel_body(text)
        if any(re.fullmatch(p, kb) for p in STUB_PATTERNS):
            stub_count += 1
            issues.append(f"{rel}: body is a passthrough stub")

        if legacy:
            # still counts as implemented for CSV coverage, but is exempt from
            # the image-profile param/test/registration conventions
            continue

        # 2. metadata
        for m in REQUIRED_META:
            if not re.search(rf"^\s*;;\s*{m}\s+\S", text, re.M):
                issues.append(f"{rel}: missing metadata {m}")
        mcat = re.search(r"^\s*;;\s*@category\s+(\S+)", text, re.M)
        if mcat:
            want = CATEGORY_DIR.get(mcat.group(1))
            if want and want != folder:
                issues.append(f"{rel}: @category {mcat.group(1)} should live in kernels/{want}/")

        # 3/4. params
        ps = re.findall(r"^\(param (\w+) (\S+) (\S+) (\S+) (\d)\)", text, re.M)
        # A Color flattens to 3 scalars and a Matrix3 to 9, so a kernel carrying
        # one of those legitimately exceeds 6. Only count params outside a group.
        group = set()
        names = [p[0] for p in ps]
        for i, nm in enumerate(names):
            for suffix in ("_r", "_g", "_b"):
                if nm.endswith(suffix) and i + 1 < len(names) and i + 2 < len(names):
                    if names[i + 1].endswith("_g") and names[i + 2].endswith("_b") \
                       and names[i + 1][: -2] == nm[: -2] and names[i + 2][: -2] == nm[: -2]:
                        group.update({i, i + 1, i + 2})
        mat = {i for i, nm in enumerate(names) if re.search(r"\bm\d\d$", nm)}
        effective = len(ps) - len(group | mat)
        if not (3 <= effective <= 6):
            issues.append(f"{rel}: {len(ps)} params ({effective} outside a Color/Matrix "
                          f"group; convention is 3-6)")
        body = text[text.rindex("(defkernel"):] if "(defkernel" in text else ""
        # A param may be used in a local defn helper rather than in the kernel body
        # itself, so search the whole file with the (param ...) lines removed.
        rest = re.sub(r"^\(param\b.*$", "", text, flags=re.M)
        for pname, dflt, lo, hi, flag in ps:
            if flag == "1" and "." in dflt:
                issues.append(f"{rel}: integer param {pname} has fractional default {dflt}")
            try:
                if not (float(lo) <= float(dflt) <= float(hi)):
                    issues.append(f"{rel}: {pname} default {dflt} outside [{lo},{hi}]")
            except ValueError:
                pass
            if not re.search(rf"\b{re.escape(pname)}\b", rest):
                issues.append(f"{rel}: param {pname} is never used anywhere in the file")

        # 5b. stub check for the legacy catalogue already ran above
        # 7. test file
        if not os.path.exists(os.path.join(ROOT, f"tests/kernels/{name}.test.jolt")):
            issues.append(f"{rel}: no tests/kernels/{name}.test.jolt")

        # 8/9. registration
        key = os.path.relpath(path, os.path.join(ROOT, "kernels"))[:-5]
        if key not in reg:
            issues.append(f"{rel}: not in kernels/CMakeLists.txt IMAGE_KERNELS")
        if key not in ver:
            issues.append(f"{rel}: not in the tests/unit verify list")

    # CSV coverage
    impl = {os.path.basename(f)[:-5] for f in files}
    missing = [r for r in rows if r["Kernel Name"] not in impl]
    impossible = [r for r in missing if r["Output Type"].strip() in UNREPRESENTABLE]
    scalarish = [r for r in missing
                 if r["Output Type"].strip() in ("Float", "Vec2", "Vec3")]

    print(f"kernel sources      : {len(files)}")
    print(f"registered          : {len(reg)}")
    print(f"passthrough stubs   : {stub_count}")
    print(f"CSV coverage        : {len(rows) - len(missing)}/{len(rows)}")
    print(f"  remaining         : {len(missing)}")
    print(f"    output type not representable by an image profile : {len(impossible)}")
    print(f"    scalar/vector output, would need a convention change: {len(scalarish)}")
    if impossible:
        print("    -> " + ", ".join(sorted(r["Kernel Name"] for r in impossible)))
    if scalarish:
        print("    -> " + ", ".join(sorted(r["Kernel Name"] for r in scalarish)))
    print()
    if issues:
        print(f"ISSUES: {len(issues)}")
        for i in issues:
            print("  " + i)
        return 1
    print("no issues")
    return 0


if __name__ == "__main__":
    sys.exit(main())
