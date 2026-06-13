(module
  ;; Hand-written sample mirroring the Phase 10.1 channel ring-buffer pattern that
  ;; emit_wasm.c generates (CHAN_INIT / CHAN_PUT). It is the executable fixture for
  ;; the WASM test harness (wasmtime / node / headless browser). Channel layout
  ;; (i32 cells): [0]=head [4]=tail [8]=cap, then `cap` data slots at offset 12.
  (memory (export "memory") 1)
  (global $arena_sp (mut i32) (i32.const 2048))

  ;; chan_init() -> i32  : bump-allocate a channel, return its base pointer
  (func $chan_init (result i32)
    (local $p i32)
    global.get $arena_sp
    local.set $p
    local.get $p i32.const 0 i32.store offset=0   ;; head = 0
    local.get $p i32.const 0 i32.store offset=4   ;; tail = 0
    local.get $p i32.const 8 i32.store offset=8   ;; cap  = 8
    global.get $arena_sp i32.const 44 i32.add global.set $arena_sp
    local.get $p)

  ;; chan_put(chan, val) : non-blocking ring-buffer store at tail, advance tail
  (func $chan_put (param $c i32) (param $v i32)
    local.get $c
    local.get $c i32.load offset=4 i32.const 4 i32.mul i32.add
    local.get $v
    i32.store offset=12
    local.get $c
    local.get $c i32.load offset=4 i32.const 1 i32.add
    local.get $c i32.load offset=8 i32.rem_u
    i32.store offset=4)

  ;; chan_get(chan) -> i32 : non-blocking ring-buffer read at head, advance head
  (func $chan_get (param $c i32) (result i32)
    (local $r i32)
    local.get $c
    local.get $c i32.load offset=0 i32.const 4 i32.mul i32.add
    i32.load offset=12
    local.set $r
    local.get $c
    local.get $c i32.load offset=0 i32.const 1 i32.add
    local.get $c i32.load offset=8 i32.rem_u
    i32.store offset=0
    local.get $r)

  ;; test() -> i32 : round-trip 42 through a channel (expected result: 42)
  (func (export "test") (result i32)
    (local $c i32)
    call $chan_init
    local.set $c
    local.get $c i32.const 42 call $chan_put
    local.get $c call $chan_get))
