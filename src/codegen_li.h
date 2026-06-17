#ifndef CODEGEN_LI_H
#define CODEGEN_LI_H

#include "parser_statements.h"
#include "parser_visibility.h"

// --- TEKO IL ISA (OPCODES) DEFINITION ---
typedef enum {
    OP_PROLOG = 0xFE,
    OP_EPILOG = 0xFF,

    OP_HALT = 0x00,
    OP_ICONST = 0x01,
    OP_SCONST = 0x02,
    OP_LOAD = 0x03,
    OP_STORE = 0x04,
    OP_ADD = 0x05,
    OP_SUB = 0x06,
    OP_MUL = 0x07,
    OP_DIV = 0x08,

    // Phase 12 (P12-E): integer modulo + comparisons ($w0 = $w0 <op> $w1, result
    // 0/1 for comparisons). Single-byte ops like ADD/SUB/MUL/DIV.
    OP_MOD = 0x0D,
    OP_EQ = 0x0E,
    OP_NE = 0x0F,
    OP_LT = 0x15,
    OP_LE = 0x16,
    OP_GT = 0x17,
    OP_GE = 0x18,

    // Phase 12 (P12-G): call a native base-encoding codec runtime function. 4-byte LE
    // codec id: 0 = base64 encode, 1 = base64 decode, 2 = hex encode, 3 = hex decode.
    // Takes the input pointer in $w0 (NUL-terminated), returns the output pointer
    // (NUL-terminated, allocated via teko_alloc) in $w0.
    OP_CALL_RUNTIME = 0x19,

    // Phase 11 (Browser FFI): call a host import declared via `extern fn … from
    // "ns" as "name"`. Carries a 4-byte little-endian import index (into the
    // module's import table); the WASM emitter lowers it to `call $import_<idx>`.
    OP_CALL_IMPORT = 0x09,

    // Phase 11 (Browser FFI MVP-2): stage an argument for a multi-param host
    // import. Carries a 4-byte little-endian arg-slot index; the WASM emitter
    // copies the accumulator ($w0) into staging local $a<slot>. OP_CALL_IMPORT
    // then pushes $a0..$a(n-2) followed by $w0 (the last arg stays in the
    // accumulator), so an N-param import needs N-1 preceding OP_SETARGs.
    OP_SETARG = 0x0A,

    // Phase 12 (Frontend Grammar): named local variables. Each carries a 4-byte
    // little-endian slot index into the function's named-local file ($v0..$vN). The
    // WASM emitter declares the locals at function open and lowers:
    //   OP_STORE_LOCAL n  →  local.set $vn   (from the accumulator $w0)
    //   OP_LOAD_LOCAL  n  →  local.get $vn → $w0
    OP_LOAD_LOCAL = 0x0B,
    OP_STORE_LOCAL = 0x0C,

    // Concurrency and Channels
    OP_SPAWN_ASYNC = 0x10,
    // Phase 14 (14.I): fire a routine with N arguments (Go-style — pass any variables, e.g. shared
    // channel handles). The args are staged into the import-arg slots $a0..$a(N-1) via OP_SETARG,
    // then OP_SPAWN_ASYNC_ARGS (4-byte argc) copies them into the spawned task's spill frame and
    // enqueues {fn=$w0, frame}. Distinct from OP_SPAWN_ASYNC (Phase-10 in-module channel spawn,
    // arg=$cp), which stays byte-identical. Inside the routine, OP_LOAD_SPAWN_ARG (4-byte idx)
    // reads the idx-th passed argument into $w0 (the frontend binds each `fn` param from it).
    OP_SPAWN_ASYNC_ARGS = 0x68, // 4-byte argc: fire slot=$w0 with $a0..$a(argc-1)
    OP_LOAD_SPAWN_ARG   = 0x69, // 4-byte idx: $w0 = the idx-th spawn argument
    OP_AWAIT_INTENT = 0x11,
    OP_CHAN_INIT = 0x12,
    OP_CHAN_PUT = 0x13,
    OP_CHAN_GET = 0x14, // Blocking channel receive (yields to the scheduler if empty)

    // Phase 10.2b: function-boundary opcodes for green-thread lowering. Each
    // routine body is emitted as a separate WASM function indexed in a table so
    // SPAWN_ASYNC can dispatch it via call_indirect. OP_FUNC_BEGIN carries a
    // 4-byte little-endian routine id (its table slot); OP_FUNC_END closes it.
    OP_FUNC_BEGIN = 0x40,
    OP_FUNC_END = 0x41,

    // Phase 14 (14.B): duplex channel ops — a dedicated opcode family (owner decision) for
    // the `duplex.*` surface. Each lowers to a teko_rt_duplex_* call on the native runner and
    // to the wasm32 runtime-reactor import on WASM (the duplex C runtime is the single source
    // of truth). Args are staged via OP_SETARG (0..n-2) + the accumulator (last), exactly like
    // OP_CALL_RUNTIME; a result (handle / value / status) lands in $w0.
    OP_DUPLEX_OPEN  = 0x42, // open(capacity) -> handle
    OP_DUPLEX_SEND  = 0x43, // send(handle, endpoint, value) -> status
    OP_DUPLEX_RECV  = 0x44, // recv(handle, endpoint) -> value
    OP_DUPLEX_POLL  = 0x45, // poll(handle, endpoint) -> status (non-consuming)
    OP_DUPLEX_CLOSE = 0x46, // close(handle) -> 0

    // Phase 14 (14.C): delayed (timed) channel ops — same dedicated-opcode + teko_rt_delayed_*
    // / reactor-import lowering as the duplex family.
    OP_DELAYED_OPEN    = 0x47, // open(capacity) -> handle
    OP_DELAYED_SEND    = 0x48, // send(handle, value, delay) -> status
    OP_DELAYED_ADVANCE = 0x49, // advance(handle, dt) -> new logical time
    OP_DELAYED_RECV    = 0x4A, // recv(handle) -> value (earliest due)
    OP_DELAYED_POLL    = 0x4B, // poll(handle) -> status (non-consuming)
    OP_DELAYED_CLOSE   = 0x4C, // close(handle) -> 0

    // Phase 14 (14.D): broadcast (non-destructive 1:N pub-sub) channel ops.
    OP_BCAST_OPEN      = 0x4D, // open(capacity) -> handle
    OP_BCAST_SUBSCRIBE = 0x4E, // subscribe(handle) -> subscriber id
    OP_BCAST_PUBLISH   = 0x4F, // publish(handle, value) -> status
    OP_BCAST_RECV      = 0x50, // recv(handle, sub_id) -> value
    OP_BCAST_POLL      = 0x51, // poll(handle, sub_id) -> status (non-consuming)
    OP_BCAST_CLOSE     = 0x52, // close(handle) -> 0

    // Phase 14 (14.E): automated shared memory — a `shared { }` block injects a coarse lock
    // (ENTER/LEAVE wrap the body), and `atomic.*` cells provide lock-free RMW counters. These
    // lower to direct teko_shared_*/teko_atomic_* calls (register-width ABI, no rt wrapper).
    OP_SHARED_ENTER  = 0x53, // acquire the coarse block lock
    OP_SHARED_LEAVE  = 0x54, // release the coarse block lock
    OP_ATOMIC_CELL   = 0x55, // atomic.cell(initial) -> handle
    OP_ATOMIC_ADD    = 0x56, // atomic.add(handle, delta) -> new value
    OP_ATOMIC_LOAD   = 0x57, // atomic.load(handle) -> value
    OP_ATOMIC_STORE  = 0x58, // atomic.store(handle, value) -> 0

    // Phase 14 (14.G): timespan waiters. The delay (canonical ms) is computed into $w0 before
    // the op. Single-byte (no IL arg). OP_WAIT is a SYNCHRONOUS sleep (native teko_rt_sleep_ms /
    // WASM env.teko_sleep host import); OP_AWAIT_FOR is a cooperative timed yield that lets queued
    // background tasks run (native teko_rt_await_ms drains the run queue; WASM records the ms via
    // env.teko_await then drains $teko_sched_run). Both clobber $w0 (a runtime call).
    OP_WAIT      = 0x59, // wait(ms)  — block the current thread for ms
    OP_AWAIT_FOR = 0x5A, // await(ms) — cooperative yield with an ms deadline

    // Phase 14 (control-flow foundation): STRUCTURED loops + branches lowered from real source —
    // native asm labels (.Lcont_/.Lbrk_/.Lendif_) + jmp/jcc, WASM structured (block $brk (loop
    // $cont …)) + (if … end). All single-byte; the backends keep a loop-id + if-id stack so
    // BREAK/CONTINUE/BREAK_IF_FALSE target the innermost loop. These break linear flow, so each is
    // a CSE barrier (added to BOTH invalidation sets in codegen_metal.c). A `while (c) { b }` is
    // LOOP_BEGIN; <c→$w0>; BREAK_IF_FALSE; <b>; LOOP_END. A `loop { b }` omits the BREAK_IF_FALSE.
    // An `if (c) { b }` is <c→$w0>; IF_BEGIN; <b>; IF_END (enters the body iff $w0 != 0).
    OP_LOOP_BEGIN     = 0x5B, // open a loop (block $brk + loop $cont; native .Lcont label)
    OP_LOOP_END       = 0x5C, // back-edge to loop top + close (native jmp .Lcont; .Lbrk:)
    OP_BREAK          = 0x5D, // unconditional break to the innermost loop exit
    OP_CONTINUE       = 0x5E, // unconditional jump to the innermost loop top
    OP_BREAK_IF_FALSE = 0x5F, // break the innermost loop when $w0 == 0 (while-condition test)
    OP_IF_BEGIN       = 0x60, // enter the if-body iff $w0 != 0 (else skip to IF_END)
    OP_IF_END         = 0x61, // close the if-body

    // Phase 14 (14.F): resilience policy ops — the `retry { } fallback { }` / `circuit { }` block
    // grammar drives the teko_retry C policy runtime (the single source of truth) through these.
    // Same dedicated-opcode + teko_rt_retry_*/teko_rt_circuit_* (native) / reactor-import (WASM)
    // lowering as the channel families; args staged via OP_SETARG with the last in $w0; the result
    // (handle / decision / delay) lands in $w0. They are $w0-clobbering runtime calls (CSE sets).
    OP_RETRY_NEW            = 0x62, // retry_new(attempts, timeout, mode, base) -> handle
    OP_RETRY_SHOULD_CONTINUE= 0x63, // should_continue(handle, attempt, elapsed) -> 0/1
    OP_RETRY_NEXT_DELAY     = 0x64, // next_delay(handle, attempt) -> ms
    OP_CIRCUIT_NEW          = 0x65, // circuit_new(threshold, cooldown) -> handle
    OP_CIRCUIT_ALLOW        = 0x66, // circuit_allow(handle, now) -> 0/1
    OP_CIRCUIT_RECORD       = 0x67, // circuit_record(handle, ok, now) -> 0

    // Phase 15 (15.A): object model ops — the `class` surface's instance store. A dedicated
    // opcode family (owner pattern, like the Phase 14 channel families): each lowers to a
    // teko_rt_object_* call on the native runner and to the wasm32 runtime-reactor import on
    // WASM (the teko_object C runtime is the single source of truth). Args are staged via
    // OP_SETARG (0..n-2) with the last in the accumulator ($w0), exactly like OP_CALL_RUNTIME;
    // a result (handle / value) lands in $w0. ZERO RUNTIME REFLECTION: field indices are
    // compile-time constants the frontend emits — there is no runtime name lookup or type tag.
    OP_OBJ_NEW  = 0x6A, // obj_new(nfields) -> handle
    OP_OBJ_SET  = 0x6B, // obj_set(handle, idx, value) -> 0
    OP_OBJ_GET  = 0x6C, // obj_get(handle, idx) -> value
    OP_OBJ_FREE = 0x6D, // obj_free(handle) -> 0

    // Phase 15 (15.A): SYNCHRONOUS table call — invoke the routine whose table slot is in $w0,
    // synchronously, and return its result in $w0 (distinct from OP_SPAWN_ASYNC*, which enqueue
    // a background task and return nothing). This is the method-dispatch primitive: a method is
    // a function-table routine taking `self` (+ params) via the spawn-arg ABI; a concrete-class
    // call lowers to ICONST <slot> (static dispatch) + OP_CALL_FUNC, while 15.B dynamic dispatch
    // will compute the slot from a compile-time vtable. Carries a 4-byte little-endian argc; args
    // are staged in $a0..$a(argc-1) via OP_SETARG, exactly like OP_SPAWN_ASYNC_ARGS. Native lowers
    // to teko_rt_call (result in rax); WASM call_indirects the $task table and reads the result
    // the callee spilled to frame[0]. Sets uses_spawn (the routine table + scheduler TU are needed).
    OP_CALL_FUNC = 0x6E, // 4-byte argc: $w0 = call slot=$w0 with args $a0..$a(argc-1)

    // Phase 15 (15.B): static vtable ops — the abstract/trait dynamic-dispatch table. Same
    // dedicated-opcode + teko_rt_vtable_* (native) / wasm reactor-import lowering as the channel
    // families (args staged via OP_SETARG 0..n-2, last in $w0; result in $w0). The teko_vtable C
    // runtime is the single source of truth. VTABLE_SET populates an entry at $main start;
    // VTABLE_GET resolves (type_id, method_id) -> routine slot at a dynamic call site, which then
    // feeds OP_CALL_FUNC. Both are $w0-clobbering runtime calls (CSE barriers).
    OP_VTABLE_SET = 0x6F, // vtable_set(type_id, method_id, slot) -> 0
    OP_VTABLE_GET = 0x70, // vtable_get(type_id, method_id) -> slot

    // Phase 17 (17.A): f64 VALUE MODEL — a PARALLEL float accumulator ($f0/$f1, native xmm0/xmm1
    // or d0/d1, WASM (local $f0 f64)/(local $f1 f64)) alongside the integer $w0/$w1. Float opcodes
    // are purely ADDITIVE and only handled by the hosted (emit_native_hosted.c) and WASM
    // (emit_wasm.c) paths + the codegen_metal.c dispatch — the 16 freestanding emitter goldens
    // never see a float opcode, so their byte streams stay byte-identical. The integer path is
    // untouched. OP_FCONST carries a 4-byte LITTLE-ENDIAN index into the float-constant pool (NOT a
    // 64-bit immediate); the pool (threaded to the backend via teko_metal_set_floats) holds the
    // f64 bit patterns, mirroring the string pool. The compares write $w0 (i32 0/1 → VT_INT); the
    // arith/move ops touch $f0/$f1 only (native scratch r11/x9, NOT rax/rbx, so $w0 is preserved).
    OP_FCONST       = 0x71, // 4-byte pool index: $f0 = float_pool[idx]
    OP_FADD         = 0x72, // $f0 = $f0 + $f1
    OP_FSUB         = 0x73, // $f0 = $f0 - $f1
    OP_FMUL         = 0x74, // $f0 = $f0 * $f1
    OP_FDIV         = 0x75, // $f0 = $f0 / $f1 (IEEE; inf/nan are defined, no zero guard)
    // Phase 17 (17.B): float modulo `%`. $f0 = fmod($f0, $f1) = $f0 - trunc($f0/$f1)*$f1 (IEEE
    // remainder toward zero). Native is libc-hosted -> `call fmod` (xmm0,xmm1 -> xmm0 / d0,d1 -> d0);
    // WASM has no f64.rem so it inlines `a - trunc(a/b)*b`. Stack-neutral; touches $f0/$f1 only.
    OP_FMOD         = 0x76, // $f0 = fmod($f0, $f1)
    OP_FEQ          = 0x77, // $w0 = ($f0 == $f1) ? 1 : 0  (result i32 → VT_INT)
    OP_FNE          = 0x78, // $w0 = ($f0 != $f1) ? 1 : 0
    OP_FLT          = 0x79, // $w0 = ($f0 <  $f1) ? 1 : 0
    OP_FLE          = 0x7A, // $w0 = ($f0 <= $f1) ? 1 : 0
    OP_FGT          = 0x7B, // $w0 = ($f0 >  $f1) ? 1 : 0
    OP_FGE          = 0x7C, // $w0 = ($f0 >= $f1) ? 1 : 0
    OP_FSTORE       = 0x7D, // $f1 = $f0  (scratch spill; mirrors OP_STORE)
    OP_FLOAD        = 0x7E, // $f0 = $f1  (mirrors OP_LOAD)
    OP_FSTORE_LOCAL = 0x7F, // 4-byte slot: $fv<slot> = $f0
    OP_FLOAD_LOCAL  = 0x80, // 4-byte slot: $f0 = $fv<slot>
    OP_I2F          = 0x81, // $f0 = (double)$w0  ($w0 unchanged) — int→float promotion
    // Phase 17 (17.B): CHECKED float->int (truncate toward zero), FAIL-LOUD. The int model is
    // i32-range ($w0 is i32 on WASM; matches parse_int). WASM lowers to `i32.trunc_f64_s`, which
    // TRAPS on NaN/±Inf/out-of-i32-range (automatic fail-loud, like 16.F's __builtin_trap). Native
    // `cvttsd2si` does NOT trap, so the hosted emitter emits an explicit NaN + i32-range guard
    // (matched to i32.trunc_f64_s's valid open interval -2147483649.0 < x < 2147483648.0) that
    // `call`s teko_rt_f2i_fail (the SAME exit-70 + stderr fail-loud path 16.F uses) — so a value
    // that traps on WASM also aborts on native. Result i32 in $w0 (clobbers $w0 with a non-const).
    OP_F2I          = 0x82, // $w0 = (i32)trunc($f0)  (checked, fail-loud)

    // Phase 17.F.3 (LIVE): the EXACT base-10 `decimal` VALUE MODEL — a SEPARATE 256-byte memory-slot
    // accumulator ($d0/$d1) alongside the integer $w0/$w1 and the f64 $f0/$f1. A decimal does NOT fit
    // a register, so it flows by POINTER into 256-byte slots: native = two stack slots + a decimal
    // frame region ($dvN); WASM = fixed 256-byte regions carved out of linear memory (gated on
    // wasm_emit_decimal so decimal-free modules stay byte-identical). The arith/move ops touch
    // $d0/$d1/$dvN only (native scratch GPRs, NOT $w0/$w1 — the integer accumulator is preserved);
    // the COMPARES write an i32 0/1 to $w0 (→ VT_INT). OP_DCONST carries a 4-byte LITTLE-ENDIAN index
    // into a NEW decimal-constant pool (256-byte blobs, threaded via teko_metal_set_decimals), exactly
    // as OP_FCONST indexes the float pool. The arith ops call teko_rt_decimal_* (fail-loud on
    // overflow/divzero); compares call teko_rt_decimal_cmp and map -1/0/+1 → 0/1. The 16 freestanding
    // emitter goldens never see a decimal opcode, so their byte streams stay byte-identical. The casts
    // 0x93–0x96 (OP_I2D/D2I/F2D/D2F) + the decimal.to_string/parse surface remain RESERVED for 17.F.4.
    OP_DCONST       = 0x83, // 4-byte pool index: $d0 = decimal_pool[idx] (256-byte copy)
    OP_DADD         = 0x84, // $d0 = $d0 + $d1   (teko_rt_decimal_add; fail-loud on overflow)
    OP_DSUB         = 0x85, // $d0 = $d0 - $d1
    OP_DMUL         = 0x86, // $d0 = $d0 * $d1
    OP_DDIV         = 0x87, // $d0 = $d0 / $d1   (fail-loud on divide-by-zero)
    OP_DMOD         = 0x88, // $d0 = $d0 % $d1   (Python Decimal.__mod__; fail-loud on modulo-by-zero)
    OP_DEQ          = 0x89, // $w0 = ($d0 == $d1) ? 1 : 0   (result i32 → VT_INT)
    OP_DNE          = 0x8A, // $w0 = ($d0 != $d1) ? 1 : 0
    OP_DLT          = 0x8B, // $w0 = ($d0 <  $d1) ? 1 : 0
    OP_DLE          = 0x8C, // $w0 = ($d0 <= $d1) ? 1 : 0
    OP_DGT          = 0x8D, // $w0 = ($d0 >  $d1) ? 1 : 0
    OP_DGE          = 0x8E, // $w0 = ($d0 >= $d1) ? 1 : 0
    OP_DSTORE       = 0x8F, // $d1 = $d0   (256-byte memcpy; scratch spill, mirrors OP_STORE/OP_FSTORE)
    OP_DLOAD        = 0x90, // $d0 = $d1   (256-byte memcpy)
    OP_DSTORE_LOCAL = 0x91, // 4-byte slot: $dv<slot> = $d0  (256-byte memcpy)
    OP_DLOAD_LOCAL  = 0x92, // 4-byte slot: $d0 = $dv<slot>  (256-byte memcpy)

    // Phase 17.F.4 (LIVE): int/float ↔ decimal CASTS. Each is single-byte and flows the decimal value
    // by pointer through the $d0 slot. The casts call the teko_rt_decimal_* cast wrappers; F2D/D2F
    // bridge through the shortest-string form (both sides correctly-rounded), I2D builds the
    // coefficient from the int, D2I truncates toward zero (matches OP_F2I) and is CHECKED/fail-loud on
    // i32-range overflow. Gated on uses_decimal so decimal-free output stays byte-identical.
    OP_I2D          = 0x93, // $d0 = (decimal)$w0   (int -> decimal, scale 0; cannot fail)
    OP_D2I          = 0x94, // $w0 = (i32)$d0       (decimal -> i32, truncate, checked fail-loud)
    OP_F2D          = 0x95, // $d0 = (decimal)$f0   (f64 -> decimal via shortest string)
    OP_D2F          = 0x96, // $f0 = (f64)$d0       (decimal -> f64 via shortest string)

    // Phase 19 (ROUTER-CORE Wave 0 — RESERVED; see teko_router.h): OP_CALL_RUNTIME id range
    // 175–179 are pre-allocated for the static radix-tree router surface. Frontend wiring
    // (lowering `api`/`middleware`/verbs to these ids) is deferred to ROUTER-NATIVE (Wave 2).
    // The engine itself (teko_router.c) is target-agnostic and already compiles into the WASM
    // reactor. No opcodes are allocated here — this comment reserves the id space only.
    //   175 = teko_router_new     (Wave 2)
    //   176 = teko_router_add     (Wave 2)
    //   177 = teko_router_dispatch (Wave 2)
    //   178 = teko_router_reset   (Wave 2)
    //   179 = (reserved / future)

    // Phase 17.F (RESERVED — owner-APPROVED, implemented after 17.A–17.E): an EXACT base-10
    // `decimal` type — a FIXED-WIDTH 256-BYTE value (~8B metadata: sign + decimal scale/exponent;
    // ~248B base-10 coefficient → ~590 significant digits, fraction ~128 bits ≈ ~38 places), banker's
    // rounding (round-half-to-even) default; exact for money, distinct from the binary f64 above.
    // Self-contained 64-bit-limb arithmetic (no heap/__int128/libc). 256B does NOT fit a register, so
    // it flows via memory-slot $d0/$d1 + teko_rt_decimal_* calls (channel/object model, not the f64
    // register accumulator). 0x83–0x92 are now LIVE enum constants (above; promoted by 17.F.3, the
    // way 0x76/0x82 were promoted by 17.B). The CASTS 0x93–0x96 remain RESERVED for 17.F.4 (NOT
    // emitted, NO enum constants, NO live token — documentation/reservation only):
    //   0x93 = OP_I2D    0x94 = OP_D2I    0x95 = OP_F2D    0x96 = OP_D2F
    // The next free contiguous opcode range therefore starts at 0x97.

    // Phase 18 (18.E.1): FIXED-size CONTIGUOUS array ops — a runtime-call family like OP_OBJ_*. The
    // args are staged in $a0.. (OP_SETARG) with the last in $w0; the result (handle / value / len)
    // lands in $w0. Lower to teko_rt_array_* (native) / the wasm32 reactor import. UNLIKE OP_OBJ_*,
    // get/set are CHECKED FAIL-LOUD on an out-of-range index (native exit 70 / WASM trap). Gated on
    // uses_array so array-free output (incl. the 16 freestanding goldens) stays byte-identical.
    OP_ARR_NEW = 0x97, // arr_new(n) -> handle (n zero-initialized cells)
    OP_ARR_GET = 0x98, // arr_get(handle, idx) -> value  (fail-loud on OOB)
    OP_ARR_SET = 0x99, // arr_set(handle, idx, value) -> 0  (fail-loud on OOB)
    OP_ARR_LEN = 0x9A, // arr_len(handle) -> length (O(1) metadata)

    // Phase 18 (18.E.2): TYPED `i32[]` PACKED numeric array ops — a SEPARATE collection from OP_ARR_*
    // (cells are PACKED int32, the SIMD substrate). Identical runtime-call family + ABI; lower to
    // teko_rt_iarray_* (native) / the wasm32 reactor import. get/set are CHECKED FAIL-LOUD on an
    // out-of-range index. Gated on uses_iarray so iarray-free output (incl. the 16 freestanding
    // goldens) stays byte-identical.
    OP_IARR_NEW = 0x9B, // iarray_new(n) -> handle (n zero-initialized packed i32 cells)
    OP_IARR_GET = 0x9C, // iarray_get(handle, idx) -> value  (fail-loud on OOB)
    OP_IARR_SET = 0x9D, // iarray_set(handle, idx, value) -> 0  (fail-loud on OOB)
    OP_IARR_LEN = 0x9E, // iarray_len(handle) -> length (O(1) metadata)

    // Phase 18 (18.E.4): REAL per-ISA SIMD reduction. simd.sum(run) reduces a contiguous typed i32[]
    // run (handle in $w0) to its scalar sum (i32 in $w0). UNLIKE the runtime-call families above, the
    // vector loop is emitted as REAL per-backend instructions (x86 SSE2 / arm64 NEON / WASM simd128;
    // scalar fallback on the 16 freestanding emitters + riscv): the backend emits ONE kernel function
    // per module (teko_simd_sum_i32, gated on uses_simd / wasm_emit_simd) and this op lowers to "get
    // the run's data pointer + length, then call that kernel". $w0-clobbering (call result). Gated so
    // simd-free output stays byte-identical (the 16 goldens never see it).
    OP_SIMD_SUM = 0x9F, // simd_sum(handle) -> scalar sum of the i32[] run (real vector kernel)

    // Control Flow and Branches
    OP_JMP = 0x20,
    OP_JMP_IF_FALSE = 0x21,
    OP_RETURN = 0x22,

    // --- NEW: BARE-METAL COMPATIBLE ARENA MANAGEMENT ---
    OP_ARENA_PUSH = 0x30, // Starts a contiguous region on silicon
    OP_ARENA_POP = 0x31   // O(1) batch cleanup via hardware
} OpCode;

// Internal structure for the constant string symbol table (String Pool)
typedef struct {
    char** strings;
    int count;
    int capacity;
} ConstantStringPool;

// Phase 11 (Browser FFI frontend): a host import the IL declares, lowered from an
// `extern fn … from "ns" as "name"` (or a `@dom`/`@js` intrinsic). The WASM backend
// turns each into an `(import "ns" "name" (func …))`; OP_CALL_IMPORT carries the
// index into this table. Kept frontend-side so the parser populates it directly.
typedef struct {
    char* ns;        // import module/namespace, e.g. "env" or "dom"
    char* name;      // imported field, e.g. "log" or "setText"
    int n_params;    // i32 params
    int has_result;  // 1 if it returns an i32
} TekoILImport;

// Structure representing the final binary bytecode buffer in memory
typedef struct {
    unsigned char* code;
    int size;
    int capacity;
    ConstantStringPool pool;
    // Phase 11: the import table populated from `extern`/`@dom` declarations.
    TekoILImport* imports;
    int import_count;
    int import_capacity;
    // Phase 12: count of named local variables ($v0..$v{local_count-1}) the program
    // uses in $main; threaded to the WASM emitter so it declares them at function open.
    int local_count;
    // Phase 12 (P12-G): 1 if the program calls a base64/hex codec, so the backend emits
    // the codec runtime functions (otherwise omitted, keeping lean modules lean).
    int uses_codec;
    // Phase 13 (13.1): 1 if the program calls a hash primitive (hash.sha256/.sha512), so
    // the backend emits the in-module SHA runtime (otherwise omitted).
    int uses_hash;
    // Phase 13 (Sub-phase C): 1 if the program calls `random.bytes` (id 41), so the WASM
    // backend declares the host entropy import (env.teko_random) + the in-module CSPRNG
    // hex wrapper. Native targets ignore this (they link the C CSPRNG via teko_rt).
    int uses_random;
    // Phase 13 (Sub-phase C): 1 if the program calls `uuid.v4`/`uuid.v7` (ids 42/43), so the
    // WASM backend declares the host entropy import (env.teko_random) + time import
    // (env.teko_now) and the self-contained v4/v7 runtime. Native targets ignore this.
    int uses_uuid_rng;
    // Phase 13 (Sub-phase C, "big step"): 1 if the program calls a crypto primitive whose
    // WASM lowering is the compiled C runtime reactor (ids 5,10-40 — every hash/HMAC/AEAD/
    // KDF/signature beyond the in-module sha256/md5/sha1/uuid set). The WASM backend then
    // imports the reactor's teko_rt_* entry points from the "crypto" module and shares one
    // linear memory with it (imported from env). Native targets ignore this (they link the
    // same C runtime directly via libteko_rt.a).
    int uses_crypto_ext;
    // Phase 14 (14.B): 1 if the program uses a `duplex.*` channel op (OP_DUPLEX_*). The native
    // runner links the duplex C runtime via teko_rt; the WASM backend imports it from the
    // runtime reactor + shares linear memory. Duplex-free programs stay byte-identical.
    int uses_duplex;
    // Phase 14 (14.C): 1 if the program uses a `delayed.*` timed-channel op (OP_DELAYED_*).
    // Same backend wiring as uses_duplex (native teko_rt link / WASM reactor import).
    int uses_delayed;
    // Phase 14 (14.D): 1 if the program uses a `broadcast.*` pub-sub op (OP_BCAST_*). Same
    // backend wiring as uses_duplex/uses_delayed.
    int uses_bcast;
    // Phase 14 (14.E): 1 if the program uses a `shared { }` block or an `atomic.*` op
    // (OP_SHARED_*/OP_ATOMIC_*). Backends link/import the teko_shared runtime.
    int uses_shared;
    // Phase 14 (14.A): 1 if the program fires background tasks via a `routines { … }`
    // block (lowered to OP_SPAWN_ASYNC). The backends then ensure the cooperative
    // scheduler is drained before the program exits: WASM emits `call $teko_sched_run`
    // at $main close; the native runner calls `teko_rt_run` at HALT and emits the
    // routine function-pointer table. Spawn-free programs stay byte-identical.
    int uses_spawn;
    // Phase 14 (14.G): 1 if the program uses `wait <ts>;` (OP_WAIT). The WASM backend declares
    // the host sleep import (env.teko_sleep); native links teko_rt_sleep_ms (always present).
    int uses_wait;
    // Phase 14 (14.G): 1 if the program uses `await <ts>;` (OP_AWAIT_FOR). The WASM backend
    // declares the host import (env.teko_await) + drains $teko_sched_run; native links
    // teko_rt_await_ms (in the scheduler TU) — also sets uses_spawn so the routine table exists.
    int uses_await;
    // Phase 14 (14.F): 1 if the program uses a `retry`/`circuit` resilience block (OP_RETRY_*/
    // OP_CIRCUIT_*). Native links teko_rt_retry_*/teko_rt_circuit_*; WASM imports them from the
    // runtime reactor + shares linear memory (same wiring as the channel families).
    int uses_retry;
    // Phase 15 (15.A): 1 if the program uses an object op (OP_OBJ_*) — i.e. instantiates a
    // `class`. Native links teko_rt_object_*; WASM imports them from the runtime reactor +
    // shares linear memory (same wiring as the channel families). Object-free programs stay
    // byte-identical.
    int uses_object;
    // Phase 15 (15.B): 1 if the program uses a static-vtable op (OP_VTABLE_*) — i.e. abstract/trait
    // dynamic dispatch. Native links teko_rt_vtable_*; WASM imports from the reactor + shared memory.
    int uses_vtable;
    // Phase 18 (18.E.1): 1 if the program uses a fixed-size array op (OP_ARR_*). Native links
    // teko_rt_array_*; WASM imports them from the runtime reactor + shares linear memory (same
    // wiring as OP_OBJ_*). Array-free programs (incl. the 16 freestanding goldens) stay
    // byte-identical.
    int uses_array;
    // Phase 18 (18.E.2): 1 if the program uses a TYPED `i32[]` packed-array op (OP_IARR_*). Native
    // links teko_rt_iarray_*; WASM imports them from the runtime reactor + shares linear memory (same
    // wiring as OP_ARR_*). iarray-free programs (incl. the 16 freestanding goldens) stay byte-identical.
    int uses_iarray;
    // Phase 18 (18.E.4): 1 if the program uses the REAL SIMD reduction (OP_SIMD_SUM). The backend then
    // emits ONE per-ISA vector kernel function (teko_simd_sum_i32) and lowers each OP_SIMD_SUM to a
    // call into it. Native: emitted as SSE2/NEON asm (scalar on other arches); WASM: a simd128 func.
    // Also implies uses_iarray (the run is a typed i32[]). simd-free output stays byte-identical.
    int uses_simd;
    // Phase 17 (17.A): the float-constant pool — f64 bit patterns indexed by OP_FCONST's 4-byte
    // arg. Mirrors the string pool (codegen_li_add_float_constant dedups by bit-equality). Threaded
    // to the backend via teko_metal_set_floats. `uses_float` is 1 once any float opcode is emitted,
    // gating the WASM `(local $f0/$f1/$fvN f64)` declarations (non-float modules stay byte-identical).
    double* floats;
    int float_count;
    int float_capacity;
    int uses_float;
    // Phase 17.F.3: the decimal-constant pool — 256-byte `teko_decimal` blobs (NOT a register-
    // width value), indexed by OP_DCONST's 4-byte arg. Mirrors the float pool, but each entry is a
    // 256-byte blob (codegen_li_add_decimal_constant dedups by 256-byte memcmp). Threaded to the
    // backend via teko_metal_set_decimals. `uses_decimal` is 1 once any decimal opcode is emitted,
    // gating the WASM decimal linear-memory region + reactor imports and the native decimal frame
    // region (decimal-free modules stay byte-identical). `decimals` is `decimal_count * 256` bytes.
    unsigned char* decimals;
    int decimal_count;
    int decimal_capacity;
    int uses_decimal;
} BytecodeBuffer;

// Public functions of the IL Bytecode Emitter
BytecodeBuffer* codegen_li_create_context(void);
void codegen_li_emit_statement(BytecodeBuffer* buffer, const StatementASTNode* stmt);
int codegen_li_add_string_constant(BytecodeBuffer* buffer, const char* str);
// Phase 17 (17.A): add an f64 constant to the float pool (deduped by exact bit-equality) and
// return its index — the 4-byte arg for OP_FCONST. Mirrors codegen_li_add_string_constant.
int codegen_li_add_float_constant(BytecodeBuffer* buffer, double value);
// Phase 17.F.3: add a 256-byte decimal constant (deduped by 256-byte memcmp) and return its index
// — the 4-byte arg for OP_DCONST. `blob` points to a 256-byte teko_decimal value (by pointer).
int codegen_li_add_decimal_constant(BytecodeBuffer* buffer, const unsigned char* blob);
void codegen_li_write_to_file(const BytecodeBuffer* buffer, const char* filename);
void codegen_li_free_context(BytecodeBuffer* buffer);

// Phase 11 (Browser FFI frontend): register a host import (deduped by ns+name) and
// return its table index — the arg for OP_CALL_IMPORT.
int codegen_li_add_import(BytecodeBuffer* buffer, const char* ns, const char* name,
                          int n_params, int has_result);

// Phase 11 (Browser FFI frontend): IL emit helpers used by the parser→IL lowering of
// the interop surface. Each appends to `buffer->code`.
void codegen_li_emit_iconst(BytecodeBuffer* buffer, int value);
void codegen_li_emit_sconst(BytecodeBuffer* buffer, int pool_index);
void codegen_li_emit_store(BytecodeBuffer* buffer); // $w1 <- $w0
void codegen_li_emit_load(BytecodeBuffer* buffer);  // $w0 <- $w1
void codegen_li_emit_store_local(BytecodeBuffer* buffer, int slot); // $vslot <- $w0
void codegen_li_emit_load_local(BytecodeBuffer* buffer, int slot);  // $w0 <- $vslot
void codegen_li_emit_binop(BytecodeBuffer* buffer, OpCode op);       // $w0 = $w0 <op> $w1
void codegen_li_emit_call_runtime(BytecodeBuffer* buffer, int codec_id); // $w0 = codec($w0)
void codegen_li_emit_setarg(BytecodeBuffer* buffer, int slot);
void codegen_li_emit_call_import(BytecodeBuffer* buffer, int import_index);
void codegen_li_emit_func_begin(BytecodeBuffer* buffer, int routine_id);
void codegen_li_emit_func_end(BytecodeBuffer* buffer);
// Phase 14 (14.A): fire the routine whose table slot is in $w0 as a background task.
// Sets buffer->uses_spawn so the backends drain the scheduler before program exit.
void codegen_li_emit_spawn_async(BytecodeBuffer* buffer);
// Phase 14 (14.I): fire the routine in $w0 with `argc` args staged in $a0..$a(argc-1)
// (OP_SPAWN_ASYNC_ARGS); sets buffer->uses_spawn. Used by `routines { worker(a, b, …); }`.
void codegen_li_emit_spawn_async_args(BytecodeBuffer* buffer, int argc);
// Phase 14 (14.I): in a routine body, load the idx-th passed spawn argument into $w0.
void codegen_li_emit_load_spawn_arg(BytecodeBuffer* buffer, int idx);
// Phase 14 (14.B): emit a duplex op (one of OP_DUPLEX_*); sets buffer->uses_duplex.
void codegen_li_emit_duplex(BytecodeBuffer* buffer, OpCode op);
// Phase 14 (14.C): emit a delayed-channel op (one of OP_DELAYED_*); sets buffer->uses_delayed.
void codegen_li_emit_delayed(BytecodeBuffer* buffer, OpCode op);
// Phase 14 (14.D): emit a broadcast op (one of OP_BCAST_*); sets buffer->uses_bcast.
void codegen_li_emit_bcast(BytecodeBuffer* buffer, OpCode op);
// Phase 14 (14.E): emit a shared/atomic op (OP_SHARED_*/OP_ATOMIC_*); sets buffer->uses_shared.
void codegen_li_emit_shared(BytecodeBuffer* buffer, OpCode op);
// Phase 14 (14.G): emit OP_WAIT (sync sleep); sets buffer->uses_wait.
void codegen_li_emit_wait(BytecodeBuffer* buffer);
// Phase 14 (14.G): emit OP_AWAIT_FOR (cooperative timed yield); sets uses_await + uses_spawn.
void codegen_li_emit_await(BytecodeBuffer* buffer);
// Phase 14 (control-flow foundation): emit a single-byte structured control-flow opcode
// (OP_LOOP_BEGIN/END, OP_BREAK[_IF_FALSE], OP_CONTINUE, OP_IF_BEGIN/END).
void codegen_li_emit_cf(BytecodeBuffer* buffer, OpCode op);
// Phase 14 (14.F): emit a resilience policy op (OP_RETRY_*/OP_CIRCUIT_*); sets buffer->uses_retry.
void codegen_li_emit_retry(BytecodeBuffer* buffer, OpCode op);
// Phase 15 (15.A): emit an object op (one of OP_OBJ_*); sets buffer->uses_object.
void codegen_li_emit_object(BytecodeBuffer* buffer, OpCode op);
// Phase 18 (18.E.1): emit a fixed-size array op (one of OP_ARR_*); sets buffer->uses_array.
void codegen_li_emit_array(BytecodeBuffer* buffer, OpCode op);
// Phase 18 (18.E.2): emit a typed `i32[]` packed-array op (one of OP_IARR_*); sets buffer->uses_iarray.
void codegen_li_emit_iarray(BytecodeBuffer* buffer, OpCode op);
// Phase 18 (18.E.4): emit the REAL SIMD reduction op (OP_SIMD_SUM); sets buffer->uses_simd (and
// uses_iarray, since the run is a typed i32[]). The backend emits the per-ISA vector kernel once.
void codegen_li_emit_simd(BytecodeBuffer* buffer, OpCode op);
// Phase 15 (15.A): synchronously call the routine in $w0 with `argc` args staged in $a0..$a(argc-1)
// (OP_CALL_FUNC); the result lands in $w0. Sets buffer->uses_spawn (routine table + scheduler).
void codegen_li_emit_call_func(BytecodeBuffer* buffer, int argc);
// Phase 15 (15.B): emit a static-vtable op (OP_VTABLE_SET/GET); sets buffer->uses_vtable.
void codegen_li_emit_vtable(BytecodeBuffer* buffer, OpCode op);
// Phase 17 (17.A): float value-model emit helpers. Each sets buffer->uses_float.
//  - emit_fconst: add `v` to the float pool, emit OP_FCONST <idx> (loads pool[idx] → $f0).
//  - emit_funop: a single-byte float op (OP_FADD..OP_FGE, OP_FSTORE, OP_FLOAD, OP_I2F).
//  - emit_fstore_local / emit_fload_local: $fv<slot> <- $f0 / $f0 <- $fv<slot> (4-byte slot).
void codegen_li_emit_fconst(BytecodeBuffer* buffer, double v);
void codegen_li_emit_funop(BytecodeBuffer* buffer, OpCode op);
void codegen_li_emit_fstore_local(BytecodeBuffer* buffer, int slot);
void codegen_li_emit_fload_local(BytecodeBuffer* buffer, int slot);
// Phase 17 (17.B): emit OP_F2I (single-byte, CHECKED float->int; fail-loud). Sets uses_float so the
// WASM backend declares the float accumulator locals even if the program only down-casts a float.
void codegen_li_emit_f2i(BytecodeBuffer* buffer);
// Phase 17.F.3: decimal value-model emit helpers. Each sets buffer->uses_decimal.
//  - emit_dconst: add the 256-byte `blob` to the decimal pool, emit OP_DCONST <idx> ($d0 = pool[idx]).
//  - emit_dunop: a single-byte decimal op (OP_DADD..OP_DGE, OP_DSTORE, OP_DLOAD).
//  - emit_dstore_local / emit_dload_local: $dv<slot> <- $d0 / $d0 <- $dv<slot> (4-byte slot).
void codegen_li_emit_dconst(BytecodeBuffer* buffer, const unsigned char* blob);
void codegen_li_emit_dunop(BytecodeBuffer* buffer, OpCode op);
void codegen_li_emit_dstore_local(BytecodeBuffer* buffer, int slot);
void codegen_li_emit_dload_local(BytecodeBuffer* buffer, int slot);
// Phase 17.F.4: emit an int/float↔decimal cast opcode (OP_I2D/OP_F2D/OP_D2I/OP_D2F). Sets
// uses_decimal (and uses_float for OP_F2D/OP_D2F). Mirrors codegen_li_emit_f2i for the float side.
void codegen_li_emit_dcast(BytecodeBuffer* buffer, OpCode op);
void codegen_li_emit_halt(BytecodeBuffer* buffer);

#endif // CODEGEN_LI_H