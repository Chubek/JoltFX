;; joltwasm ABI 1. Every exported callable takes (args, argc, result)->status.
(module
  (import "joltfx" "call" (func $call (param i32 i32 i32 i32 i32) (result i32)))
  (import "joltfx" "clamp" (func $clamp (param f64 f64 f64) (result f64)))
  (memory (export "memory") 1)
  (global (export "jfx_abi_version") i32 (i32.const 1))
  (global (export "jfx_scratch") i32 (i32.const 4096))
  (global (export "jfx_scratch_size") i32 (i32.const 4096))
  (data (i32.const 0) "command")
  (data (i32.const 16) "grade.add")
  (data (i32.const 32) "grade.param")
  (data (i32.const 64) "grade_primary")
  (data (i32.const 80) "exposure")

  (func (export "gain") (param $args i32) (param $argc i32) (param $out i32) (result i32)
    local.get $argc i32.const 1 i32.ne
    if i32.const -6 return end
    local.get $args i32.load i32.const 3 i32.ne
    if i32.const -6 return end
    local.get $out i32.const 3 i32.store
    local.get $out
    local.get $args f64.load offset=8 f64.const 2 f64.mul
    f64.const 0 f64.const 1 call $clamp
    f64.store offset=8
    i32.const 0)

  ;; Six argument records at 128: operation, track, clip, target, value, text.
  (func $command (param $op i32) (param $oplen i32) (param $text i32)
      (param $textlen i32) (param $value f64) (result i32)
    i32.const 128 i32.const 0 i32.const 224 memory.fill
    i32.const 128 i32.const 10 i32.store
    i32.const 136 local.get $op i32.store
    i32.const 140 local.get $oplen i32.store
    i32.const 160 i32.const 2 i32.store
    i32.const 192 i32.const 2 i32.store
    i32.const 224 i32.const 2 i32.store
    i32.const 256 i32.const 3 i32.store
    i32.const 264 local.get $value f64.store
    i32.const 288 i32.const 10 i32.store
    i32.const 296 local.get $text i32.store
    i32.const 300 local.get $textlen i32.store
    i32.const 0 i32.const 7 i32.const 128 i32.const 6 i32.const 320 call $call)

  (func (export "edit") (param i32 i32 i32) (result i32) (local $status i32)
    i32.const 16 i32.const 9 i32.const 64 i32.const 13 f64.const 0 call $command
    local.tee $status if local.get $status return end
    i32.const 32 i32.const 11 i32.const 80 i32.const 8 f64.const 1 call $command))
