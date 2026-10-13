#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/joltfx-storage.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
"${KOTLINC:-kotlinc}" "$root/app/src/main/java/org/joltfx/mobile/DocumentFiles.kt" \
    "$root/tests/DocumentFilesTest.kt" -include-runtime -d "$work/storage-tests.jar"
"${JAVA_HOME:+$JAVA_HOME/bin/}java" -jar "$work/storage-tests.jar"
