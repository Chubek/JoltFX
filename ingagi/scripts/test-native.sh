#!/usr/bin/env bash
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
build=${INGAGI_NATIVE_BUILD:-/tmp/opencode/ingagi-novoparse}
cmake -S "$root/third_party/novoparse" -B "$build" -DBUILD_TESTING=OFF
cmake --build "$build" --parallel 4
build=$(cd -- "$build" && pwd)

# Check that committed generated artifacts still match their grammar.
"$build/novoparse" generate "$root/ingagi/frontend/ingagi.ng" --profile C -o "$build/ingagi.h"
"$build/novoparse" compile "$root/ingagi/frontend/ingagi.ng" -o "$build/ingagi.sexp"
cmp "$build/ingagi.h" "$root/ingagi/frontend/generated/ingagi.h"
cmp "$build/ingagi.sexp" "$root/ingagi/frontend/ingagi.sexp"

cd -- "$root/ingagi"
GOROOT=/usr/lib/go CGO_ENABLED=1 GOPROXY=off GOTOOLCHAIN=local \
    CGO_LDFLAGS="-L$build -Wl,-rpath,$build ${CGO_LDFLAGS:-}" \
    go test -tags novoparse ./... -timeout 120s
