#!/bin/sh
# CLI integration tests for Phase 3.
# Usage: test_cli.sh <joltfx-binary> <repo-root>
# Exits non-zero on the first failure.
set -eu

# Resolve to an absolute path up front. Several checks run the CLI from inside
# $TMP, so a relative path given on the command line would fail there and
# nowhere else.
CLI="$1"
case "$CLI" in
    /*) ;;
    *) CLI="$(cd "$(dirname "$CLI")" && pwd)/$(basename "$CLI")" ;;
esac
if [ ! -x "$CLI" ]; then
    echo "error: '$1' is not an executable CLI binary" >&2
    exit 1
fi
ROOT="$2"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

pass=0
fail=0

check_ok() {
    desc="$1"
    shift
    status=0
    "$@" >"$TMP/out.log" 2>&1 || status=$?
    if [ "$status" -eq 0 ]; then
        pass=$((pass + 1))
        echo "ok: $desc"
    else
        fail=$((fail + 1))
        echo "FAIL: $desc (exit $status)"
        cat "$TMP/out.log"
    fi
}

check_fail() {
    desc="$1"
    shift
    if "$@" >"$TMP/out.log" 2>&1; then
        fail=$((fail + 1))
        echo "FAIL: $desc (expected failure, got success)"
        cat "$TMP/out.log"
    else
        pass=$((pass + 1))
        echo "ok: $desc (correctly failed)"
    fi
}

HELLO="$ROOT/examples/hello_world/effect.jolt"
BRIGHT="$ROOT/kernels/color/brightness.jolt"

check_ok "version" "$CLI" version
check_ok "--version" "$CLI" --version
check_ok "help" "$CLI" help
check_ok "--help" "$CLI" --help
check_ok "help compile" "$CLI" help compile
check_ok "help render" "$CLI" help render

check_ok "verify hello_world" "$CLI" verify "$HELLO"
check_ok "verify brightness" "$CLI" verify "$BRIGHT"
check_fail "verify missing file" "$CLI" verify "$TMP/does-not-exist.jolt"

printf '(defkernel broken [x] (+ x))' > "$TMP/broken.jolt"
check_fail "verify broken kernel" "$CLI" verify "$TMP/broken.jolt"

check_ok "compile hello_world" "$CLI" compile "$HELLO" -o "$TMP/hello.jbc"
if [ ! -s "$TMP/hello.jbc" ]; then
    fail=$((fail + 1))
    echo "FAIL: compile wrote empty bytecode"
else
    pass=$((pass + 1))
    echo "ok: compile wrote nonempty bytecode"
fi
check_fail "compile missing file" "$CLI" compile "$TMP/does-not-exist.jolt"

check_ok "effects lists 12" sh -c "\"$CLI\" effects > \"$TMP/effects.log\" && [ \"\$(wc -l < \"$TMP/effects.log\")\" -eq 12 ]"
for name in brightness contrast invert grayscale saturation sepia opacity threshold posterize exposure tint gamma; do
    if ! grep -qx "$name" "$TMP/effects.log"; then
        fail=$((fail + 1))
        echo "FAIL: effects list missing $name"
    fi
done
pass=$((pass + 1))
echo "ok: effects list contains all 12 kernels"

check_ok "info effect" "$CLI" info brightness
check_ok "info file" "$CLI" info "$BRIGHT"
check_fail "info unknown" "$CLI" info does_not_exist_effect

check_ok "render defaults" sh -c "cd \"$TMP\" && \"$CLI\" render -o render_default.ppm"
check_ok "render invert 16x16" "$CLI" render --effect invert --width 16 --height 16 -o "$TMP/invert.ppm"
check_ok "render brightness param" "$CLI" render --effect brightness --param 2.0 --width 8 --height 8 -o "$TMP/bright.ppm"
check_fail "render unknown effect" "$CLI" render --effect missing -o "$TMP/bad.ppm"
check_fail "render bad backend" "$CLI" render --backend directx9 -o "$TMP/bad2.ppm"
check_fail "render bad width" "$CLI" render --width 0 -o "$TMP/bad3.ppm"

# PPM magic check on one render.
if head -c 2 "$TMP/invert.ppm" | grep -q "P6"; then
    pass=$((pass + 1))
    echo "ok: render wrote P6 PPM"
else
    fail=$((fail + 1))
    echo "FAIL: render output is not P6 PPM"
fi

# Every bundled effect must render through the CLI.
for name in brightness contrast invert grayscale saturation sepia opacity threshold posterize exposure tint gamma; do
    check_ok "render $name" "$CLI" render --effect "$name" --width 4 --height 4 -o "$TMP/$name.ppm"
done

# `render` must go through the engine's backend, not a private CPU path. The
# summary line reports the device the frame actually ran on.
"$CLI" render --width 4 --height 4 -o "$TMP/which.ppm" > "$TMP/render_which.log" 2>&1
if grep -q "backend " "$TMP/render_which.log"; then
    pass=$((pass + 1))
    echo "ok: render reports the resolved backend"
else
    fail=$((fail + 1))
    echo "FAIL: render did not report a backend"
fi

# The capability report must be honest: the headless frontend has no loop, no
# project I/O and no selection, and must say so.
check_ok "capabilities" "$CLI" capabilities
"$CLI" capabilities > "$TMP/caps.log" 2>&1
for op in render export playback viewport_state; do
    if ! grep -qE "^  $op +yes" "$TMP/caps.log"; then
        fail=$((fail + 1))
        echo "FAIL: capabilities does not report $op"
    fi
done
for op in run open_project save_project selection; do
    if ! grep -qE "^  $op +no " "$TMP/caps.log"; then
        fail=$((fail + 1))
        echo "FAIL: capabilities claims to implement $op"
    fi
done
pass=$((pass + 1))
echo "ok: capability report is accurate"

# export writes a PPM sequence through the same render path.
mkdir -p "$TMP/frames"
check_ok "export frames" "$CLI" export --effect tint --param 0.5 --width 8 --height 8 \
    --start 0 --end 3 -o "$TMP/frames"
for n in 000000 000001 000002 000003; do
    if [ ! -s "$TMP/frames/frame_$n.ppm" ]; then
        fail=$((fail + 1))
        echo "FAIL: export did not write frame_$n.ppm"
    elif ! head -c 2 "$TMP/frames/frame_$n.ppm" | grep -q "P6"; then
        fail=$((fail + 1))
        echo "FAIL: exported frame_$n.ppm is not P6"
    fi
done
pass=$((pass + 1))
echo "ok: export wrote the requested frame sequence"
check_fail "export bad range" "$CLI" export --start 5 --end 1 -o "$TMP/frames"
check_fail "export missing dir option" "$CLI" export --width 0 -o "$TMP/frames"

check_ok "color grading catalog" "$CLI" grade list
check_ok "color calibration catalog" "$CLI" calibration list
printf 'P6\n1 1\n255\n\100\040\020' > "$TMP/color.ppm"
check_ok "kernel-backed exposure" "$CLI" grade apply grade_primary "$TMP/color.ppm" "$TMP/graded.ppm" exposure=1
printf 'P6\n1 1\n255\n\200\100\040' > "$TMP/expected.ppm"
check_ok "exposure pixels" cmp "$TMP/graded.ppm" "$TMP/expected.ppm"
check_fail "wrong color section" "$CLI" calibration apply grade_primary "$TMP/color.ppm" "$TMP/bad.ppm"
check_fail "bad color parameter" "$CLI" grade apply grade_primary "$TMP/color.ppm" "$TMP/bad.ppm" exposure=100
check_fail "missing color LUT" "$CLI" grade apply grade_lut "$TMP/color.ppm" "$TMP/bad.ppm" --lut "$TMP/missing.cube"

check_ok "new rational-rate NLE sequence" "$CLI" nle new "$TMP/new.jfx" --size 2 1 --fps 30000 1001
cat > "$TMP/edit.txt" <<'EOF'
clip.add 0 0 0 12
clip.split 0 0 0 6
clip.move 0 1 0 20
clip.slip 0 1 0 3
clip.duplicate 0 1 0 40
clip.ripple_delete 0 1 0 0
undo
redo
calibration.add 0 1 0 0 calib_lut
grade.add 0 1 0 0 grade_primary
grade.param 0 1 0 1 exposure
save
EOF
check_ok "terminal NLE and color edit pipeline" "$CLI" nle edit "$TMP/new.jfx" "$TMP/edit.jfx" < "$TMP/edit.txt"
check_ok "NLE JSON state" "$CLI" nle info "$TMP/edit.jfx"
"$CLI" nle info "$TMP/edit.jfx" > "$TMP/nle-state.json"
check_ok "NLE rational rate persisted" grep -q '"fpsNum":30000,"fpsDen":1001' "$TMP/nle-state.json"
check_ok "NLE ripple position persisted" grep -q '"start":34,"length":6,"inPoint":9' "$TMP/nle-state.json"
check_ok "NLE scaled export" "$CLI" nle render "$TMP/edit.jfx" -o "$TMP/nle-frame" --start 34 --end 35 --width 1 --height 1
check_ok "NLE exported frame exists" test -s "$TMP/nle-frame0034.ppm"
check_fail "NLE bad raster" "$CLI" nle new "$TMP/bad.jfx" --size 0 10
check_fail "NLE bad rate" "$CLI" nle new "$TMP/bad.jfx" --fps 30 0
check_fail "NLE bad export range" "$CLI" nle render "$TMP/edit.jfx" -o "$TMP/bad" --start -1

check_ok "new composition" "$CLI" compose new "$TMP/comp.jfx" --size 1 1
cat > "$TMP/nodes.txt" <<'EOF'
node.add 0 0 0 0 color
node.param 1 0 0 0 g
node.param 1 0 0 0 b
node.connect 1 0 0 0
node.position 0 0 0 -12.25 123.5
node.label 0 0 0 0 Output #1 with spaces
node.duplicate 1 0 0 0
node.param 2 0 0 0 r
node.remove 2 0 0 0
undo
redo
composition
save
EOF
check_ok "terminal composition edit pipeline" "$CLI" compose edit "$TMP/comp.jfx" "$TMP/comp-edited.jfx" < "$TMP/nodes.txt"
check_ok "composition output and layout JSON" "$CLI" compose info "$TMP/comp-edited.jfx"
"$CLI" compose info "$TMP/comp-edited.jfx" > "$TMP/comp-state.json"
check_ok "composition explicit output persisted" grep -q '"output":0' "$TMP/comp-state.json"
check_ok "composition node layout persisted" grep -q '"x":-12.25,"y":123.5' "$TMP/comp-state.json"
check_ok "composition export" "$CLI" compose render "$TMP/comp-edited.jfx" -o "$TMP/composed.ppm"
printf 'P6\n1 1\n255\n\377\000\000' > "$TMP/comp-expected.ppm"
check_ok "composition export pixels" cmp "$TMP/composed.ppm" "$TMP/comp-expected.ppm"
check_ok "composition interior node preview" "$CLI" compose render "$TMP/comp-edited.jfx" -o "$TMP/comp-node.ppm" --node 1 --time 2 --size 2 1
check_fail "composition invalid raster" "$CLI" compose new "$TMP/bad.jfx" --size 4097 1
check_fail "composition invalid preview time" "$CLI" compose render "$TMP/comp-edited.jfx" -o "$TMP/bad.ppm" --time nan
check_fail "composition invalid output node" "$CLI" compose render "$TMP/comp-edited.jfx" -o "$TMP/bad.ppm" --node 99
check_fail "composition rejects sequence state" "$CLI" compose info "$TMP/edit.jfx"

# Legacy run entry point still works.
check_ok "run" "$CLI" run

echo "---"
echo "passed=$pass failed=$fail"
[ "$fail" -eq 0 ]
