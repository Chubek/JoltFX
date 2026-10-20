(target (schema 1) (id WASM) (kind native) (status implemented)
 (driver wasm) (description "Standalone WebAssembly scalar binary emitter")
 (artifacts wasm) (platforms wasm32) (requires go)
 (features i64 bool functions recursion control-flow))
