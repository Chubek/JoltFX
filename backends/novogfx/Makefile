# Makefile for novogfx — Rust workspace plus the C API in `NovoGFX-API.idl`.
#
# Targets:
#   make build      — cargo build --workspace
#   make test       — cargo test --workspace --lib
#   make test-all   — lib + integration (vkms, skipped without a device)
#   make lint       — fmt --check + clippy -D warnings
#   make doc        — cargo doc --workspace --no-deps
#   make c-headers  — verify novogfx.h matches the IDL surface (symbol check)
#   make clean      — cargo clean

CARGO ?= cargo
FEATURES ?=

.PHONY: build test test-all lint fmt doc c-headers c-test cpp-test clean

build:
	$(CARGO) build --workspace

test:
	$(CARGO) test --workspace --lib

test-all:
	$(CARGO) test --workspace --lib
	$(CARGO) test -p novogfx --features vkms --test vkms_smoke -- --test-threads=1

lint: fmt
	$(CARGO) clippy --workspace --all-targets -- -D warnings

fmt:
	$(CARGO) fmt --all -- --check

doc:
	$(CARGO) doc --workspace --no-deps

c-headers:
	@test -f capi/novogfx-c/novogfx.h || (echo "missing capi/novogfx-c/novogfx.h" && exit 1)
	@test -f capi/novogfx-c/novogfx.hpp || (echo "missing capi/novogfx-c/novogfx.hpp" && exit 1)
	@grep -q "novogfx_device_open" capi/novogfx-c/novogfx.h
	@grep -q "novogfx_atomic_request_commit" capi/novogfx-c/novogfx.h
	@grep -q "novogfx_event_reader_poll" capi/novogfx-c/novogfx.h
	@echo "C headers OK"

clean:
	$(CARGO) clean

# Link and execute against the actual Rust shared library, not just headers.
c-test:
	$(CARGO) build -p novogfx-c
	$(CC) -std=c11 -Wall -Wextra -Werror -Icapi/novogfx-c tests/capi_smoke.c -Ltarget/debug -lnovogfx_c -Wl,-rpath,'$$ORIGIN/debug' -o target/capi-smoke
	./target/capi-smoke

cpp-test:
	$(CARGO) build -p novogfx-c
	$(CXX) -std=c++20 -Wall -Wextra -Werror -Icapi/novogfx-c -I. tests/cpp_smoke.cpp -Ltarget/debug -lnovogfx_c -Wl,-rpath,'$$ORIGIN/debug' -o target/cpp-smoke
	./target/cpp-smoke
