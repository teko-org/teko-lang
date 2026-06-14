(module
  ;; Phase 10.4 Layer B reference (hand-written mirror of the emitter's
  ;; `--target=...-wasm-threads` output). Real multicore: the module imports a
  ;; SHARED memory; SPAWN delegates to a host `teko_rt.spawn(fn, arg)` that starts
  ;; a Worker / worker_threads thread, which re-instantiates this module against
  ;; the same shared memory and calls the exported `teko_invoke` dispatcher. The
  ;; hand-off uses the atomics proposal. Channel cell: [0]=ready flag, [4]=value.
  (import "env" "memory" (memory 1 1 shared))
  (import "teko_rt" "spawn" (func $teko_spawn (param i32 i32)))
  (global $arena_sp (mut i32) (i32.const 2048))
  (type $task (func (param i32)))

  ;; Worker thread (table slot 0): atomically publish 777, then wake the waiter.
  (func $worker (param $arg i32)
    (i32.atomic.store offset=4 (local.get $arg) (i32.const 777))
    (i32.atomic.store offset=0 (local.get $arg) (i32.const 1))
    (drop (memory.atomic.notify offset=0 (local.get $arg) (i32.const 1))))

  ;; Main thread: init the atomic cell, spawn the worker, block until ready, read.
  (func $main (export "main") (result i32)
    (local $cp i32) (local $spins i32)
    (local.set $cp (i32.const 2048))
    (i32.atomic.store offset=0 (local.get $cp) (i32.const 0))   ;; ready = 0
    (call $teko_spawn (i32.const 0) (local.get $cp))            ;; host starts a real Worker
    ;; Notify-free atomic busy-poll: a cross-instance memory.atomic.notify was
    ;; observed never to reach the waiter on some runtimes, so we do not rely on
    ;; it. Cross-thread visibility of the shared memory is guaranteed, so the
    ;; atomic load is certain to observe the producer's flag store. Bounded by
    ;; ~2e9 spins -> unreachable, so a stuck producer traps instead of spinning
    ;; forever.
    (block $ready
      (loop $spin
        (br_if $ready (i32.eq (i32.atomic.load offset=0 (local.get $cp)) (i32.const 1)))
        (local.set $spins (i32.add (local.get $spins) (i32.const 1)))
        (if (i32.gt_u (local.get $spins) (i32.const 2000000000)) (then unreachable))
        (br $spin)))
    (i32.atomic.load offset=4 (local.get $cp)))

  (table 1 funcref)
  (elem (i32.const 0) $worker)
  ;; Dispatcher the spawned Worker calls to run a routine by table index.
  (func $teko_invoke (export "teko_invoke") (param $fn i32) (param $arg i32)
    (call_indirect (type $task) (local.get $arg) (local.get $fn)))
  (export "memory" (memory 0)))
