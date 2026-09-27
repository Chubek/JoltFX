#!/bin/sh
# CLI integration tests for Phase 3.
# Usage: test_cli.sh <joltfx-binary> <repo-root>
# Exits non-zero on the first failure.
set -eu

CLI="$1"
ROOT="$2"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT TERM

pass=0
fail=0

check_ok() {
    desc="$1"
    shift
    if "$@" >"$TMP/out.log" 2>&1; then
        pass=$((pass + 1))
        echo "ok: $desc"
    else
        fail=$((fail + 1))
        echo "FAIL: $desc (exit $?)"
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

# Legacy run entry point still works.
check_ok "run" "$CLI" run

echo "---"
echo "passed=$pass failed=$fail"
[ "$fail" -eq 0 ]
