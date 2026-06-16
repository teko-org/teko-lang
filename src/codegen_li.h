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
} BytecodeBuffer;

// Public functions of the IL Bytecode Emitter
BytecodeBuffer* codegen_li_create_context(void);
void codegen_li_emit_statement(BytecodeBuffer* buffer, const StatementASTNode* stmt);
int codegen_li_add_string_constant(BytecodeBuffer* buffer, const char* str);
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
void codegen_li_emit_halt(BytecodeBuffer* buffer);

#endif // CODEGEN_LI_H