#include "../codegen_metal.h"
#include "../emit_native_hosted.h" // teko_native_runtime_symbol — single id→symbol+arity source
#include <stdio.h>
#include <string.h>

// Phase 13 (Sub-phase C, "big step"): the crypto runtime ids whose WASM lowering is the
// compiled-C reactor (crypto.wasm), NOT an in-module WAT runtime. Everything in the
// OP_CALL_RUNTIME table with id >= 4 EXCEPT the in-module set {4 sha256, 6 md5, 7 sha1,
// 8/9 uuid v3/v5, 41 random, 42/43 uuid v4/v7}. For these ids the emitted module imports
// the reactor's teko_rt_* entry point from the "crypto" namespace (resolved, with arity,
// via teko_native_runtime_symbol — the same table the native runner uses).
static int wasm_is_crypto_ext_id(int id) {
    switch (id) {
        case 5: case 10: case 11: case 12: case 15: case 16:        // sha512/384, sha3, blake
        case 17: case 18: case 19:                                  // HMAC
        case 20: case 21: case 22: case 23:                         // AEAD
        case 24: case 25: case 26:                                  // Ed25519, X25519
        case 27: case 28:                                           // KDF
        case 29: case 30: case 31: case 32:                         // ECDSA
        case 33: case 34:                                           // SHAKE
        case 37: case 38: case 39: case 40:                         // RSA
            return 1;
        default: return 0;
    }
}

// ===========================================================================
// WASM (WAT) emitter — Phase 10.3: cooperative concurrency with mid-function
// suspension. Green threads are compiled to state machines that can suspend in
// the *middle* of their body (at a blocking channel receive), return control to
// the scheduler so other tasks run, and later resume exactly where they left off
// — WebAssembly has no stack-switching, so this is done with an explicit state
// machine + a per-task spill frame in linear memory, not a native stack switch.
//
// Register model (every function declares the same three i32 locals so the linear
// IL is stack-neutral and each function ends with exactly one value):
//   $w0  accumulator / result      $w1  scratch / running value      $cp  channel ptr
//
// Green-thread ABI:  (func $routine_N (param $arg i32) (param $state i32)
//                                     (param $frame i32) (result i32))
//   $arg    the channel base handed to the thread at spawn
//   $state  resume point (0 = start); the body br_tables on it
//   $frame  per-task spill area: [0]=$w0 [4]=$w1, preserved across suspensions
//   result  the next state: 0 = completed, k>0 = suspended at yield point k
//
// Scheduler:  $teko_sched_run drains a run queue (16-byte slots at offset 64:
//   {fn@0, arg@4, state@8, frame@12}); a task that returns state>0 is re-enqueued
//   to resume later. $main is the root (not a task) and blocks re-entrantly.
//
// Memory map (1 page = 64 KiB): [64..) run queue · [1024] .data · [2048..) arena
// (channels + per-task frames bump-allocated here).
// ===========================================================================

#define TEKO_WASM_FRAME_BYTES 64
// Linear-memory base for the string constant pool (the [1024..2048) .data region,
// below the arena at 2048 and above the run queue at 64).
#define TEKO_WASM_DATA_BASE 1024
// Phase 11 MVP-4: the real freeing allocator's heap region. Fixed partition above
// the cooperative bump arena (which starts at 2048 and grows up) so the two never
// alias for the FFI demos. The page is 64 KiB ((memory 1)); the heap is the top
// [16384..65536). A growable/unified arena+heap is future work (documented).
#define TEKO_WASM_HEAP_BASE 16384
#define TEKO_WASM_HEAP_END  65536

// Phase 11: byte offset of constant-pool string `idx` within the packed (data ...)
// segment (each string is NUL-terminated, laid out in order from TEKO_WASM_DATA_BASE).
static int teko_wasm_string_offset(const MetalContext* ctx, int idx) {
    int off = TEKO_WASM_DATA_BASE;
    for (int j = 0; j < idx && j < ctx->wasm_string_count; j++) {
        off += (int)strlen(ctx->wasm_strings[j]) + 1; // +1 for the NUL
    }
    return off;
}

// Emit a C string as WAT data bytes (no surrounding quotes): printable ASCII
// verbatim, `"`/`\` escaped, everything else as \HH.
static void emit_wat_escaped(FILE* f, const char* s) {
    for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') fprintf(f, "\\%c", c);
        else if (c >= 0x20 && c <= 0x7e) fputc((int)c, f);
        else fprintf(f, "\\%02X", c);
    }
}

// Phase 11: declare the host import table — one `(import "ns" "name" (func …))`
// per FFI entry, named $import_<idx> so OP_CALL_IMPORT can call it by name.
static void emit_wasm_imports(MetalContext* ctx) {
    FILE* f = ctx->file;
    for (int i = 0; i < ctx->wasm_import_count; i++) {
        const TekoWasmImport* im = &ctx->wasm_imports[i];
        fprintf(f, "  (import \"%s\" \"%s\" (func $import_%d", im->ns, im->name, i);
        for (int p = 0; p < im->n_params; p++) fprintf(f, " (param i32)");
        if (im->has_result) fprintf(f, " (result i32)");
        fprintf(f, "))\n");
    }
}

// Phase 11 (Browser FFI MVP-2): the minimal, honest DOM vocabulary this MVP
// supports. Each entry maps an imported `dom.<name>` to its auto-generated JS
// body. Strings cross the boundary as (ptr,len) read out of linear memory via
// TextDecoder (read-only direction — no allocator needed yet); DOM nodes cross
// as opaque i32 handles into a JS-side table. Anything not listed here is not
// covered by MVP-2 and is skipped by the glue generator.
typedef struct { const char* name; const char* body; } TekoDomGlueEntry;
static const TekoDomGlueEntry TEKO_DOM_GLUE[] = {
    { "createElement",
      "(p, n) => { const el = document.createElement(str(p, n)); handles.push(el); return handles.length - 1; }" },
    { "getElementById",
      "(p, n) => { handles.push(document.getElementById(str(p, n))); return handles.length - 1; }" },
    { "setText",
      "(h, p, n) => { handles[h].textContent = str(p, n); }" },
    { "appendChild",
      "(par, ch) => { handles[par].appendChild(handles[ch]); }" },
    // MVP-3: JS->Teko. Register a DOM listener that, when fired, calls back into
    // the module via teko_invoke(handler_fn_index, attached_handle).
    { "on",
      "(h, p, n, fn) => { handles[h].addEventListener(str(p, n), () => invoke(fn, h)); }" },
    // MVP-4: rich event payload. Marshals the event target's .value into wasm memory
    // via the real allocator and calls back with (ptr, len) instead of just a handle.
    { "on_value",
      "(h, p, n, fn) => { handles[h].addEventListener(str(p, n), (e) => { const v = String((e.target && e.target.value) || \"\"); const a = allocStr(v); invoke2(fn, a[0], a[1]); }); }" },
};

int teko_metal_emit_dom_glue(MetalContext* ctx, const char* path) {
    if (!ctx || !path) return 1;
    FILE* g = fopen(path, "w");
    if (!g) return 1;

    fprintf(g, "// AUTO-GENERATED by the Teko WASM backend (Browser FFI). Do not edit.\n");
    fprintf(g, "// Implements the dom.* host imports this module declares, with (ptr,len)\n");
    fprintf(g, "// string marshalling over the module's linear memory and an i32->Element\n");
    fprintf(g, "// handle table. `getMemory` is a thunk returning the instance's exported\n");
    fprintf(g, "// WebAssembly.Memory; `getInstance` is a thunk returning the instance (so\n");
    fprintf(g, "// DOM listeners can call back into exports.teko_invoke). Both resolve lazily\n");
    fprintf(g, "// because they are only needed after instantiation.\n");
    fprintf(g, "export function makeTekoDomImports(getMemory, getInstance) {\n");
    fprintf(g, "  const handles = [null]; // 1-based: handle 0 is the null node\n");
    fprintf(g, "  const dec = new TextDecoder();\n");
    fprintf(g, "  const enc = new TextEncoder();\n");
    fprintf(g, "  const str = (p, n) => dec.decode(new Uint8Array(getMemory().buffer, p >>> 0, n >>> 0));\n");
    fprintf(g, "  const invoke = (fn, arg) => getInstance().exports.teko_invoke(fn >>> 0, arg >>> 0);\n");
    fprintf(g, "  const invoke2 = (fn, a, b) => getInstance().exports.teko_invoke2(fn >>> 0, a >>> 0, b >>> 0);\n");
    // Materialize a JS string into wasm memory via the real allocator; returns [ptr, len].
    fprintf(g, "  const allocStr = (s) => { const b = enc.encode(s); const q = getInstance().exports.teko_alloc(b.length);\n");
    fprintf(g, "    new Uint8Array(getMemory().buffer, q, b.length).set(b); return [q, b.length]; };\n");
    fprintf(g, "  return { dom: {\n");
    for (int i = 0; i < ctx->wasm_import_count; i++) {
        const TekoWasmImport* im = &ctx->wasm_imports[i];
        if (!im->ns || strcmp(im->ns, "dom") != 0) continue;
        for (size_t k = 0; k < sizeof(TEKO_DOM_GLUE) / sizeof(TEKO_DOM_GLUE[0]); k++) {
            if (strcmp(im->name, TEKO_DOM_GLUE[k].name) == 0) {
                fprintf(g, "    %s: %s,\n", TEKO_DOM_GLUE[k].name, TEKO_DOM_GLUE[k].body);
                break;
            }
        }
    }
    fprintf(g, "  } };\n}\n");
    fclose(g);
    return 0;
}

int teko_metal_emit_facade(MetalContext* ctx, const char* path, const char* glue_module,
                           const TekoWasmFacadeEntry* entries, int count) {
    if (!ctx || !path || !glue_module) return 1;
    FILE* m = fopen(path, "w");
    if (!m) return 1;

    fprintf(m, "// AUTO-GENERATED by the Teko WASM backend (Browser FFI MVP-4). Do not edit.\n");
    fprintf(m, "// Ergonomic facade: instantiate(wasmBytes) wires the glue + module and returns\n");
    fprintf(m, "// an object whose methods take plain JS strings — each is marshalled into wasm\n");
    fprintf(m, "// memory via the real allocator (teko_alloc) and dispatched to its Teko routine.\n");
    fprintf(m, "import { makeTekoDomImports } from \"%s\";\n\n", glue_module);
    fprintf(m, "export async function instantiate(wasmBytes) {\n");
    fprintf(m, "  let memory, instance;\n");
    fprintf(m, "  const imports = makeTekoDomImports(() => memory, () => instance);\n");
    fprintf(m, "  ({ instance } = await WebAssembly.instantiate(wasmBytes, imports));\n");
    fprintf(m, "  memory = instance.exports.memory;\n");
    fprintf(m, "  const enc = new TextEncoder();\n");
    fprintf(m, "  const allocStr = (s) => { const b = enc.encode(String(s));\n");
    fprintf(m, "    const p = instance.exports.teko_alloc(b.length);\n");
    fprintf(m, "    new Uint8Array(memory.buffer, p, b.length).set(b); return [p, b.length]; };\n");
    fprintf(m, "  return {\n");
    fprintf(m, "    instance,\n");
    fprintf(m, "    main: () => instance.exports.main(),\n");
    for (int i = 0; i < count; i++) {
        // mod.<name>(str): marshal the string, then invoke the Teko routine (ptr,len).
        fprintf(m, "    %s: (s) => { const a = allocStr(s); return instance.exports.teko_invoke2(%d, a[0], a[1]); },\n",
                entries[i].name, entries[i].fn_index);
    }
    fprintf(m, "  };\n}\n");
    fclose(m);
    return 0;
}

static void emit_wasm_scheduler_runtime(FILE* f) {
    fprintf(f, "  (global $rq_head (mut i32) (i32.const 0))\n");
    fprintf(f, "  (global $rq_tail (mut i32) (i32.const 0))\n");
    fprintf(f, "  (type $task (func (param i32 i32 i32) (result i32)))\n");

    // enqueue {fn, arg, state, frame} into run-queue slot rq_tail (16-byte slots).
    fprintf(f, "  (func $teko_enqueue (param $fn i32) (param $arg i32) (param $st i32) (param $fr i32) (local $b i32)\n");
    fprintf(f, "    (local.set $b (i32.add (i32.const 64) (i32.mul (global.get $rq_tail) (i32.const 16))))\n");
    fprintf(f, "    (i32.store offset=0  (local.get $b) (local.get $fn))\n");
    fprintf(f, "    (i32.store offset=4  (local.get $b) (local.get $arg))\n");
    fprintf(f, "    (i32.store offset=8  (local.get $b) (local.get $st))\n");
    fprintf(f, "    (i32.store offset=12 (local.get $b) (local.get $fr))\n");
    fprintf(f, "    (global.set $rq_tail (i32.add (global.get $rq_tail) (i32.const 1))))\n");

    // Cooperative scheduler: dispatch each ready task via call_indirect; if it
    // returns a suspended state (>0) re-enqueue it (with the same arg+frame) so
    // it resumes once a producer has made progress. rq_head advances *before* the
    // call so a re-entrant scheduler call never re-runs the in-flight task.
    fprintf(f, "  (func $teko_sched_run (local $b i32) (local $f i32) (local $a i32) (local $st i32) (local $fr i32) (local $ret i32)\n");
    fprintf(f, "    (block $done (loop $L\n");
    fprintf(f, "      (br_if $done (i32.ge_u (global.get $rq_head) (global.get $rq_tail)))\n");
    fprintf(f, "      (local.set $b (i32.add (i32.const 64) (i32.mul (global.get $rq_head) (i32.const 16))))\n");
    fprintf(f, "      (local.set $f  (i32.load offset=0  (local.get $b)))\n");
    fprintf(f, "      (local.set $a  (i32.load offset=4  (local.get $b)))\n");
    fprintf(f, "      (local.set $st (i32.load offset=8  (local.get $b)))\n");
    fprintf(f, "      (local.set $fr (i32.load offset=12 (local.get $b)))\n");
    fprintf(f, "      (global.set $rq_head (i32.add (global.get $rq_head) (i32.const 1)))\n");
    fprintf(f, "      (local.set $ret (call_indirect (type $task) (local.get $a) (local.get $st) (local.get $fr) (local.get $f)))\n");
    fprintf(f, "      (if (i32.gt_u (local.get $ret) (i32.const 0))\n");
    fprintf(f, "        (then (call $teko_enqueue (local.get $f) (local.get $a) (local.get $ret) (local.get $fr))))\n");
    fprintf(f, "      (br $L))))\n");

    // Phase 11 (Browser FFI MVP-3): JS->Teko entry point. The host (auto-generated
    // glue) calls the exported $teko_invoke(fn_index, arg) from a DOM event handler;
    // it dispatches table slot fn_index via call_indirect with `arg` as both the
    // routine arg and a one-slot scratch frame (frame[0]=arg) so the callee loads
    // $w0=arg on entry — the same routine ABI the cooperative scheduler uses. A
    // callback runs to completion (no suspension), so its returned state is ignored.
    fprintf(f, "  (func $teko_invoke (export \"teko_invoke\") (param $fn i32) (param $arg i32) (result i32)\n");
    fprintf(f, "    global.get $arena_sp\n    local.get $arg\n    i32.store offset=0\n"); // frame[0] = arg
    fprintf(f, "    local.get $arg\n");                                                   // routine $arg (-> $cp)
    fprintf(f, "    i32.const 0\n");                                                      // state = 0 (fresh entry)
    fprintf(f, "    global.get $arena_sp\n");                                             // frame ptr
    fprintf(f, "    local.get $fn\n");
    fprintf(f, "    call_indirect (type $task))\n");

    // Phase 11 (Browser FFI MVP-4): two-argument JS->Teko entry. Marshalled data
    // (e.g. a string materialized via teko_alloc) reaches the callee as
    // $w0=a0, $w1=a1 (frame[0]=a0, frame[4]=a1 — the routine reloads both). Used by
    // the ergonomic facade (mod.greet(str) -> teko_invoke2(fn, ptr, len)) and rich
    // event payloads. Returns the routine's result.
    fprintf(f, "  (func $teko_invoke2 (export \"teko_invoke2\") (param $fn i32) (param $a0 i32) (param $a1 i32) (result i32)\n");
    fprintf(f, "    global.get $arena_sp\n    local.get $a0\n    i32.store offset=0\n");   // frame[0] = a0
    fprintf(f, "    global.get $arena_sp\n    local.get $a1\n    i32.store offset=4\n");   // frame[4] = a1
    fprintf(f, "    local.get $a0\n");                                                     // routine $arg (-> $cp)
    fprintf(f, "    i32.const 0\n");                                                       // state = 0
    fprintf(f, "    global.get $arena_sp\n");                                              // frame ptr
    fprintf(f, "    local.get $fn\n");
    fprintf(f, "    call_indirect (type $task))\n");
}

// Phase 11 MVP-4: a real freeing allocator in linear memory — distinct from the
// non-freeing cooperative bump arena. An implicit free-list over the heap region
// [HEAP_BASE..HEAP_END): each block is an 8-byte header { [0]=payload size,
// [4]=free flag } followed by its payload. teko_alloc is first-fit with splitting;
// teko_free validates the pointer (real, currently-used block start) then marks it
// and runs a forward-coalescing pass (merging adjacent free blocks) so repeated
// alloc/free does not fragment the heap into dust. null / wild / interior / double
// frees are safe no-ops.
// teko_reset bulk-reclaims the whole region. Lazily initialized as one big free
// block on first use. Exported for the host (JS materializes data here) and usable
// from Teko. Bounds: stays within the single (memory 1) page; OOM returns 0.
static void emit_wasm_heap_runtime(FILE* f) {
    fprintf(f, "  (global $heap_inited (mut i32) (i32.const 0))\n");

    // Lazy init: one free block spanning the whole heap region.
    fprintf(f, "  (func $teko_heap_init\n");
    fprintf(f, "    (if (i32.eqz (global.get $heap_inited)) (then\n");
    fprintf(f, "      (i32.store offset=0 (i32.const %d) (i32.const %d))\n",
            TEKO_WASM_HEAP_BASE, TEKO_WASM_HEAP_END - TEKO_WASM_HEAP_BASE - 8); // size
    fprintf(f, "      (i32.store offset=4 (i32.const %d) (i32.const 1))\n", TEKO_WASM_HEAP_BASE); // free
    fprintf(f, "      (global.set $heap_inited (i32.const 1)))))\n");

    // teko_alloc(n): first-fit; align n up to 8; split the block if the remainder
    // can hold a header + a minimal (8-byte) payload. Returns the payload ptr, or 0.
    fprintf(f, "  (func $teko_alloc (export \"teko_alloc\") (param $n i32) (result i32)\n");
    fprintf(f, "    (local $cur i32) (local $sz i32) (local $nb i32)\n");
    fprintf(f, "    call $teko_heap_init\n");
    fprintf(f, "    (local.set $n (i32.and (i32.add (local.get $n) (i32.const 7)) (i32.const -8)))\n");
    fprintf(f, "    (local.set $cur (i32.const %d))\n", TEKO_WASM_HEAP_BASE);
    fprintf(f, "    (block $done (loop $L\n");
    fprintf(f, "      (br_if $done (i32.ge_u (local.get $cur) (i32.const %d)))\n", TEKO_WASM_HEAP_END);
    fprintf(f, "      (local.set $sz (i32.load offset=0 (local.get $cur)))\n");
    fprintf(f, "      (if (i32.and (i32.load offset=4 (local.get $cur)) (i32.ge_u (local.get $sz) (local.get $n))) (then\n");
    fprintf(f, "        (if (i32.ge_u (local.get $sz) (i32.add (local.get $n) (i32.const 16))) (then\n");
    fprintf(f, "          (local.set $nb (i32.add (i32.add (local.get $cur) (i32.const 8)) (local.get $n)))\n");
    fprintf(f, "          (i32.store offset=0 (local.get $nb) (i32.sub (i32.sub (local.get $sz) (local.get $n)) (i32.const 8)))\n");
    fprintf(f, "          (i32.store offset=4 (local.get $nb) (i32.const 1))\n");
    fprintf(f, "          (i32.store offset=0 (local.get $cur) (local.get $n))))\n");
    fprintf(f, "        (i32.store offset=4 (local.get $cur) (i32.const 0))\n");
    fprintf(f, "        (return (i32.add (local.get $cur) (i32.const 8)))))\n");
    fprintf(f, "      (local.set $cur (i32.add (i32.add (local.get $cur) (i32.const 8)) (local.get $sz)))\n");
    fprintf(f, "      (br $L)))\n");
    fprintf(f, "    (i32.const 0))\n"); // OOM

    // teko_free(ptr): VALIDATE then free. The block header is at ptr-8; we walk the
    // block list and only free if ptr-8 is a real, currently-used block start. So
    // null, out-of-range/wild pointers, interior/misaligned pointers, and double
    // frees are all safe no-ops (no corruption). On a valid free we mark the block
    // and run a single forward-coalescing sweep merging adjacent free blocks.
    fprintf(f, "  (func $teko_free (export \"teko_free\") (param $ptr i32)\n");
    fprintf(f, "    (local $tgt i32) (local $cur i32) (local $sz i32) (local $nxt i32) (local $ok i32)\n");
    fprintf(f, "    (if (i32.eqz (local.get $ptr)) (then (return)))\n");
    fprintf(f, "    (local.set $tgt (i32.sub (local.get $ptr) (i32.const 8)))\n");
    fprintf(f, "    (if (i32.or (i32.lt_u (local.get $tgt) (i32.const %d))\n", TEKO_WASM_HEAP_BASE);
    fprintf(f, "                (i32.ge_u (local.get $tgt) (i32.const %d))) (then (return)))\n", TEKO_WASM_HEAP_END);
    // Validate $tgt is a real block boundary that is currently used.
    fprintf(f, "    (local.set $cur (i32.const %d))\n", TEKO_WASM_HEAP_BASE);
    fprintf(f, "    (block $v (loop $V\n");
    fprintf(f, "      (br_if $v (i32.ge_u (local.get $cur) (i32.const %d)))\n", TEKO_WASM_HEAP_END);
    fprintf(f, "      (br_if $v (i32.gt_u (local.get $cur) (local.get $tgt)))\n");   // stepped past → invalid
    fprintf(f, "      (if (i32.eq (local.get $cur) (local.get $tgt)) (then\n");
    fprintf(f, "        (if (i32.eqz (i32.load offset=4 (local.get $cur))) (then\n"); // used → free it
    fprintf(f, "          (i32.store offset=4 (local.get $cur) (i32.const 1))\n");
    fprintf(f, "          (local.set $ok (i32.const 1))))\n");
    fprintf(f, "        (br $v)))\n");
    fprintf(f, "      (local.set $cur (i32.add (i32.add (local.get $cur) (i32.const 8)) (i32.load offset=0 (local.get $cur))))\n");
    fprintf(f, "      (br $V)))\n");
    fprintf(f, "    (if (i32.eqz (local.get $ok)) (then (return)))\n");             // invalid / double-free → no-op
    // Forward-coalescing sweep.
    fprintf(f, "    (local.set $cur (i32.const %d))\n", TEKO_WASM_HEAP_BASE);
    fprintf(f, "    (block $done (loop $L\n");
    fprintf(f, "      (br_if $done (i32.ge_u (local.get $cur) (i32.const %d)))\n", TEKO_WASM_HEAP_END);
    fprintf(f, "      (local.set $sz (i32.load offset=0 (local.get $cur)))\n");
    fprintf(f, "      (local.set $nxt (i32.add (i32.add (local.get $cur) (i32.const 8)) (local.get $sz)))\n");
    // Guard the load of the next header behind the bounds check (i32.and is eager).
    fprintf(f, "      (if (i32.and (i32.load offset=4 (local.get $cur)) (i32.lt_u (local.get $nxt) (i32.const %d))) (then\n", TEKO_WASM_HEAP_END);
    fprintf(f, "        (if (i32.load offset=4 (local.get $nxt)) (then\n");
    fprintf(f, "          (i32.store offset=0 (local.get $cur)\n");
    fprintf(f, "            (i32.add (i32.add (local.get $sz) (i32.const 8)) (i32.load offset=0 (local.get $nxt))))\n");
    fprintf(f, "          (br $L)))))\n");                                  // merged: retry at same $cur
    fprintf(f, "      (local.set $cur (local.get $nxt))\n");
    fprintf(f, "      (br $L))))\n");

    // teko_reset(): bulk-reclaim the whole heap (region reset, the cheap path).
    fprintf(f, "  (func $teko_reset (export \"teko_reset\")\n");
    fprintf(f, "    (global.set $heap_inited (i32.const 0))\n");
    fprintf(f, "    (call $teko_heap_init))\n");
}

// Phase 12 (P12-G): native base64/hex codec runtime. Pure in-module functions (no host
// imports, no external deps) that take a NUL-terminated input pointer, allocate the
// output via teko_alloc, and return a NUL-terminated output pointer. Verified against
// RFC 4648 vectors + round-trips. The frontend lowers base64.encode/.decode and
// hex.encode/.decode to OP_CALL_RUNTIME -> these functions.
// Shared runtime helpers used by both the base codecs (P12-G) and the hash runtime
// (13.1): NUL-terminated strlen and a nibble->lowercase-hex-ASCII converter. Emitted
// once when either subsystem is active so the two never double-define a symbol.
static void emit_wasm_runtime_common(FILE* f) {
    fprintf(f, "  (func $teko_strlen (param $p i32) (result i32) (local $n i32)\n");
    fprintf(f, "    (block $d (loop $L\n");
    fprintf(f, "      (br_if $d (i32.eqz (i32.load8_u (i32.add (local.get $p) (local.get $n)))))\n");
    fprintf(f, "      (local.set $n (i32.add (local.get $n) (i32.const 1))) (br $L)))\n");
    fprintf(f, "    (local.get $n))\n");
    fprintf(f, "  (func $teko_hexc (param $v i32) (result i32)\n");
    fprintf(f, "    (if (i32.lt_u (local.get $v) (i32.const 10)) (then (return (i32.add (local.get $v) (i32.const 48)))))\n");
    fprintf(f, "    (i32.add (local.get $v) (i32.const 87)))\n");
}

static void emit_wasm_codec_runtime(FILE* f) {
    // 6-bit value -> base64 ASCII (flat if/return style — easy to balance)
    fprintf(f, "  (func $teko_b64_enc6 (param $v i32) (result i32)\n");
    fprintf(f, "    (if (i32.lt_u (local.get $v) (i32.const 26)) (then (return (i32.add (local.get $v) (i32.const 65)))))\n");
    fprintf(f, "    (if (i32.lt_u (local.get $v) (i32.const 52)) (then (return (i32.add (local.get $v) (i32.const 71)))))\n");
    fprintf(f, "    (if (i32.lt_u (local.get $v) (i32.const 62)) (then (return (i32.sub (local.get $v) (i32.const 4)))))\n");
    fprintf(f, "    (if (i32.eq (local.get $v) (i32.const 62)) (then (return (i32.const 43))))\n");
    fprintf(f, "    (i32.const 47))\n");
    // base64 ASCII -> 6-bit value
    fprintf(f, "  (func $teko_b64_dec6 (param $c i32) (result i32)\n");
    fprintf(f, "    (if (i32.and (i32.ge_u (local.get $c) (i32.const 65)) (i32.le_u (local.get $c) (i32.const 90))) (then (return (i32.sub (local.get $c) (i32.const 65)))))\n");
    fprintf(f, "    (if (i32.and (i32.ge_u (local.get $c) (i32.const 97)) (i32.le_u (local.get $c) (i32.const 122))) (then (return (i32.sub (local.get $c) (i32.const 71)))))\n");
    fprintf(f, "    (if (i32.and (i32.ge_u (local.get $c) (i32.const 48)) (i32.le_u (local.get $c) (i32.const 57))) (then (return (i32.add (local.get $c) (i32.const 4)))))\n");
    fprintf(f, "    (if (i32.eq (local.get $c) (i32.const 43)) (then (return (i32.const 62))))\n");
    fprintf(f, "    (i32.const 63))\n");
    // hex ascii -> nibble ($teko_hexc lives in the shared common runtime)
    fprintf(f, "  (func $teko_hexv (param $c i32) (result i32)\n");
    fprintf(f, "    (if (i32.and (i32.ge_u (local.get $c) (i32.const 48)) (i32.le_u (local.get $c) (i32.const 57))) (then (return (i32.sub (local.get $c) (i32.const 48)))))\n");
    fprintf(f, "    (if (i32.and (i32.ge_u (local.get $c) (i32.const 97)) (i32.le_u (local.get $c) (i32.const 102))) (then (return (i32.sub (local.get $c) (i32.const 87)))))\n");
    fprintf(f, "    (i32.sub (local.get $c) (i32.const 55)))\n");
    // base64 encode
    fprintf(f, "  (func $teko_base64_encode (export \"teko_base64_encode\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $len i32) (local $out i32) (local $i i32) (local $o i32) (local $b0 i32) (local $b1 i32) (local $b2 i32)\n");
    fprintf(f, "    (local.set $len (call $teko_strlen (local.get $in)))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.add (i32.mul (i32.div_u (i32.add (local.get $len) (i32.const 2)) (i32.const 3)) (i32.const 4)) (i32.const 1))))\n");
    fprintf(f, "    (block $d (loop $L\n");
    fprintf(f, "      (br_if $d (i32.ge_u (local.get $i) (local.get $len)))\n");
    fprintf(f, "      (local.set $b0 (i32.load8_u (i32.add (local.get $in) (local.get $i))))\n");
    fprintf(f, "      (local.set $b1 (if (result i32) (i32.lt_u (i32.add (local.get $i) (i32.const 1)) (local.get $len)) (then (i32.load8_u (i32.add (local.get $in) (i32.add (local.get $i) (i32.const 1))))) (else (i32.const 0))))\n");
    fprintf(f, "      (local.set $b2 (if (result i32) (i32.lt_u (i32.add (local.get $i) (i32.const 2)) (local.get $len)) (then (i32.load8_u (i32.add (local.get $in) (i32.add (local.get $i) (i32.const 2))))) (else (i32.const 0))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $o)) (call $teko_b64_enc6 (i32.and (i32.shr_u (local.get $b0) (i32.const 2)) (i32.const 63))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.add (local.get $o) (i32.const 1))) (call $teko_b64_enc6 (i32.and (i32.or (i32.shl (local.get $b0) (i32.const 4)) (i32.shr_u (local.get $b1) (i32.const 4))) (i32.const 63))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.add (local.get $o) (i32.const 2))) (if (result i32) (i32.lt_u (i32.add (local.get $i) (i32.const 1)) (local.get $len)) (then (call $teko_b64_enc6 (i32.and (i32.or (i32.shl (local.get $b1) (i32.const 2)) (i32.shr_u (local.get $b2) (i32.const 6))) (i32.const 63)))) (else (i32.const 61))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.add (local.get $o) (i32.const 3))) (if (result i32) (i32.lt_u (i32.add (local.get $i) (i32.const 2)) (local.get $len)) (then (call $teko_b64_enc6 (i32.and (local.get $b2) (i32.const 63)))) (else (i32.const 61))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 3))) (local.set $o (i32.add (local.get $o) (i32.const 4))) (br $L)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.const 0)) (local.get $out))\n");
    // base64 decode
    fprintf(f, "  (func $teko_base64_decode (export \"teko_base64_decode\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $len i32) (local $out i32) (local $i i32) (local $o i32) (local $c0 i32) (local $c1 i32) (local $c2 i32) (local $c3 i32)\n");
    fprintf(f, "    (local.set $len (call $teko_strlen (local.get $in)))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.add (i32.mul (i32.div_u (local.get $len) (i32.const 4)) (i32.const 3)) (i32.const 1))))\n");
    fprintf(f, "    (block $d (loop $L\n");
    fprintf(f, "      (br_if $d (i32.gt_u (i32.add (local.get $i) (i32.const 4)) (local.get $len)))\n");
    fprintf(f, "      (local.set $c0 (call $teko_b64_dec6 (i32.load8_u (i32.add (local.get $in) (local.get $i)))))\n");
    fprintf(f, "      (local.set $c1 (call $teko_b64_dec6 (i32.load8_u (i32.add (local.get $in) (i32.add (local.get $i) (i32.const 1))))))\n");
    fprintf(f, "      (local.set $c2 (i32.load8_u (i32.add (local.get $in) (i32.add (local.get $i) (i32.const 2)))))\n");
    fprintf(f, "      (local.set $c3 (i32.load8_u (i32.add (local.get $in) (i32.add (local.get $i) (i32.const 3)))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.and (i32.or (i32.shl (local.get $c0) (i32.const 2)) (i32.shr_u (local.get $c1) (i32.const 4))) (i32.const 255)))\n");
    fprintf(f, "      (local.set $o (i32.add (local.get $o) (i32.const 1)))\n");
    fprintf(f, "      (if (i32.ne (local.get $c2) (i32.const 61)) (then\n");
    fprintf(f, "        (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.and (i32.or (i32.shl (local.get $c1) (i32.const 4)) (i32.shr_u (call $teko_b64_dec6 (local.get $c2)) (i32.const 2))) (i32.const 255)))\n");
    fprintf(f, "        (local.set $o (i32.add (local.get $o) (i32.const 1)))))\n");
    fprintf(f, "      (if (i32.ne (local.get $c3) (i32.const 61)) (then\n");
    fprintf(f, "        (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.and (i32.or (i32.shl (call $teko_b64_dec6 (local.get $c2)) (i32.const 6)) (call $teko_b64_dec6 (local.get $c3))) (i32.const 255)))\n");
    fprintf(f, "        (local.set $o (i32.add (local.get $o) (i32.const 1)))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 4))) (br $L)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.const 0)) (local.get $out))\n");
    // hex encode
    fprintf(f, "  (func $teko_hex_encode (export \"teko_hex_encode\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $len i32) (local $out i32) (local $i i32) (local $b i32)\n");
    fprintf(f, "    (local.set $len (call $teko_strlen (local.get $in)))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.add (i32.mul (local.get $len) (i32.const 2)) (i32.const 1))))\n");
    fprintf(f, "    (block $d (loop $L\n");
    fprintf(f, "      (br_if $d (i32.ge_u (local.get $i) (local.get $len)))\n");
    fprintf(f, "      (local.set $b (i32.load8_u (i32.add (local.get $in) (local.get $i))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.mul (local.get $i) (i32.const 2))) (call $teko_hexc (i32.shr_u (local.get $b) (i32.const 4))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.add (i32.mul (local.get $i) (i32.const 2)) (i32.const 1))) (call $teko_hexc (i32.and (local.get $b) (i32.const 15))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $L)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.mul (local.get $len) (i32.const 2))) (i32.const 0)) (local.get $out))\n");
    // hex decode
    fprintf(f, "  (func $teko_hex_decode (export \"teko_hex_decode\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $len i32) (local $out i32) (local $i i32) (local $o i32)\n");
    fprintf(f, "    (local.set $len (call $teko_strlen (local.get $in)))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.add (i32.div_u (local.get $len) (i32.const 2)) (i32.const 1))))\n");
    fprintf(f, "    (block $d (loop $L\n");
    fprintf(f, "      (br_if $d (i32.gt_u (i32.add (local.get $i) (i32.const 2)) (local.get $len)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.and (i32.or (i32.shl (call $teko_hexv (i32.load8_u (i32.add (local.get $in) (local.get $i)))) (i32.const 4)) (call $teko_hexv (i32.load8_u (i32.add (local.get $in) (i32.add (local.get $i) (i32.const 1)))))) (i32.const 255)))\n");
    fprintf(f, "      (local.set $o (i32.add (local.get $o) (i32.const 1))) (local.set $i (i32.add (local.get $i) (i32.const 2))) (br $L)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (local.get $o)) (i32.const 0)) (local.get $out))\n");
}

// Phase 13 (13.1): native in-module SHA-256 (FIPS 180-4). No host imports, no external
// deps — the produced module computes the digest itself, mirroring the KAT-tested C
// runtime (src/runtime/teko_crypto_sha2.c) that is the algorithm's source of truth. Takes
// a NUL-terminated input pointer, returns a NUL-terminated 64-char lowercase hex digest in
// teko_alloc'd memory. All scratch (padded message, schedule, K table) is heap-allocated
// to avoid colliding with the run queue / string pool / arena. The frontend lowers
// hash.sha256(x) to OP_CALL_RUNTIME id 4 -> this function. Uses WASM's native i32.rotr.
static void emit_wasm_hash_runtime(FILE* f) {
    static const uint32_t K[64] = {
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    };
    static const uint32_t IV[8] = {
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    };
    int i;

    // K table, built once into heap memory and cached in a global pointer.
    fprintf(f, "  (global $sha256_k (mut i32) (i32.const 0))\n");
    fprintf(f, "  (func $teko_sha256_k (result i32) (local $p i32)\n");
    fprintf(f, "    (if (global.get $sha256_k) (then (return (global.get $sha256_k))))\n");
    fprintf(f, "    (local.set $p (call $teko_alloc (i32.const 256)))\n");
    for (i = 0; i < 64; ++i) {
        fprintf(f, "    (i32.store offset=%d (local.get $p) (i32.const 0x%08x))\n", i * 4, K[i]);
    }
    fprintf(f, "    (global.set $sha256_k (local.get $p)) (local.get $p))\n");

    // Main: hash.sha256(in) -> hex string pointer.
    fprintf(f, "  (func $teko_sha256_hex (export \"teko_sha256_hex\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $len i32) (local $nb i32) (local $msg i32) (local $padlen i32)\n");
    fprintf(f, "    (local $w i32) (local $kp i32) (local $i i32) (local $t i32) (local $blk i32)\n");
    fprintf(f, "    (local $bitlen i32) (local $out i32) (local $x i32) (local $s0 i32) (local $s1 i32) (local $t1 i32) (local $t2 i32)\n");
    fprintf(f, "    (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $e i32) (local $f i32) (local $g i32) (local $h i32)\n");
    fprintf(f, "    (local $h0 i32) (local $h1 i32) (local $h2 i32) (local $h3 i32) (local $h4 i32) (local $h5 i32) (local $h6 i32) (local $h7 i32)\n");
    fprintf(f, "    (local.set $len (call $teko_strlen (local.get $in)))\n");
    fprintf(f, "    (local.set $kp (call $teko_sha256_k))\n");
    // nb = (len + 72) / 64 (room for 0x80 + 8-byte length); padlen = nb*64.
    fprintf(f, "    (local.set $nb (i32.div_u (i32.add (local.get $len) (i32.const 72)) (i32.const 64)))\n");
    fprintf(f, "    (local.set $padlen (i32.mul (local.get $nb) (i32.const 64)))\n");
    fprintf(f, "    (local.set $msg (call $teko_alloc (local.get $padlen)))\n");
    fprintf(f, "    (local.set $w (call $teko_alloc (i32.const 256)))\n");
    // Zero the padded message buffer.
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $zd (loop $zl\n");
    fprintf(f, "      (br_if $zd (i32.ge_u (local.get $i) (local.get $padlen)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $msg) (local.get $i)) (i32.const 0))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $zl)))\n");
    // Copy input bytes.
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $cd (loop $cl\n");
    fprintf(f, "      (br_if $cd (i32.ge_u (local.get $i) (local.get $len)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $msg) (local.get $i)) (i32.load8_u (i32.add (local.get $in) (local.get $i))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $cl)))\n");
    // 0x80 terminator + big-endian 64-bit bit length in the final 8 bytes.
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (local.get $len)) (i32.const 0x80))\n");
    fprintf(f, "    (local.set $bitlen (i32.shl (local.get $len) (i32.const 3)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 5))) (i32.and (i32.shr_u (local.get $len) (i32.const 29)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 4))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 24)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 3))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 16)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 2))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 8)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 1))) (i32.and (local.get $bitlen) (i32.const 255)))\n");
    // State = IV.
    for (i = 0; i < 8; ++i) {
        fprintf(f, "    (local.set $h%d (i32.const 0x%08x))\n", i, IV[i]);
    }
    // For each 64-byte block.
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $bd (loop $bl\n");
    fprintf(f, "      (br_if $bd (i32.ge_u (local.get $i) (local.get $nb)))\n");
    fprintf(f, "      (local.set $blk (i32.add (local.get $msg) (i32.mul (local.get $i) (i32.const 64))))\n");
    // w[0..15] from the block, big-endian.
    fprintf(f, "      (local.set $t (i32.const 0))\n");
    fprintf(f, "      (block $wd (loop $wl\n");
    fprintf(f, "        (br_if $wd (i32.ge_u (local.get $t) (i32.const 16)))\n");
    fprintf(f, "        (i32.store (i32.add (local.get $w) (i32.shl (local.get $t) (i32.const 2)))\n");
    fprintf(f, "          (i32.or (i32.or (i32.shl (i32.load8_u (i32.add (local.get $blk) (i32.shl (local.get $t) (i32.const 2)))) (i32.const 24))\n");
    fprintf(f, "                          (i32.shl (i32.load8_u (i32.add (local.get $blk) (i32.add (i32.shl (local.get $t) (i32.const 2)) (i32.const 1)))) (i32.const 16)))\n");
    fprintf(f, "                  (i32.or (i32.shl (i32.load8_u (i32.add (local.get $blk) (i32.add (i32.shl (local.get $t) (i32.const 2)) (i32.const 2)))) (i32.const 8))\n");
    fprintf(f, "                          (i32.load8_u (i32.add (local.get $blk) (i32.add (i32.shl (local.get $t) (i32.const 2)) (i32.const 3)))))))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $wl)))\n");
    // w[16..63] = schedule.
    fprintf(f, "      (local.set $t (i32.const 16))\n");
    fprintf(f, "      (block $sd (loop $sl\n");
    fprintf(f, "        (br_if $sd (i32.ge_u (local.get $t) (i32.const 64)))\n");
    fprintf(f, "        (local.set $x (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 15)) (i32.const 2)))))\n");
    fprintf(f, "        (local.set $s0 (i32.xor (i32.xor (i32.rotr (local.get $x) (i32.const 7)) (i32.rotr (local.get $x) (i32.const 18))) (i32.shr_u (local.get $x) (i32.const 3))))\n");
    fprintf(f, "        (local.set $x (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 2)) (i32.const 2)))))\n");
    fprintf(f, "        (local.set $s1 (i32.xor (i32.xor (i32.rotr (local.get $x) (i32.const 17)) (i32.rotr (local.get $x) (i32.const 19))) (i32.shr_u (local.get $x) (i32.const 10))))\n");
    fprintf(f, "        (i32.store (i32.add (local.get $w) (i32.shl (local.get $t) (i32.const 2)))\n");
    fprintf(f, "          (i32.add (i32.add (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 16)) (i32.const 2)))) (local.get $s0))\n");
    fprintf(f, "                   (i32.add (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 7)) (i32.const 2)))) (local.get $s1))))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $sl)))\n");
    // Working vars = state.
    fprintf(f, "      (local.set $a (local.get $h0)) (local.set $b (local.get $h1)) (local.set $c (local.get $h2)) (local.set $d (local.get $h3))\n");
    fprintf(f, "      (local.set $e (local.get $h4)) (local.set $f (local.get $h5)) (local.set $g (local.get $h6)) (local.set $h (local.get $h7))\n");
    // 64 rounds.
    fprintf(f, "      (local.set $t (i32.const 0))\n");
    fprintf(f, "      (block $rd (loop $rl\n");
    fprintf(f, "        (br_if $rd (i32.ge_u (local.get $t) (i32.const 64)))\n");
    fprintf(f, "        (local.set $s1 (i32.xor (i32.xor (i32.rotr (local.get $e) (i32.const 6)) (i32.rotr (local.get $e) (i32.const 11))) (i32.rotr (local.get $e) (i32.const 25))))\n");
    fprintf(f, "        (local.set $t1 (i32.add (i32.add (i32.add (local.get $h) (local.get $s1))\n");
    fprintf(f, "                          (i32.xor (i32.and (local.get $e) (local.get $f)) (i32.and (i32.xor (local.get $e) (i32.const -1)) (local.get $g))))\n");
    fprintf(f, "                          (i32.add (i32.load (i32.add (local.get $kp) (i32.shl (local.get $t) (i32.const 2))))\n");
    fprintf(f, "                                   (i32.load (i32.add (local.get $w) (i32.shl (local.get $t) (i32.const 2)))))))\n");
    fprintf(f, "        (local.set $s0 (i32.xor (i32.xor (i32.rotr (local.get $a) (i32.const 2)) (i32.rotr (local.get $a) (i32.const 13))) (i32.rotr (local.get $a) (i32.const 22))))\n");
    fprintf(f, "        (local.set $t2 (i32.add (local.get $s0)\n");
    fprintf(f, "                          (i32.xor (i32.xor (i32.and (local.get $a) (local.get $b)) (i32.and (local.get $a) (local.get $c))) (i32.and (local.get $b) (local.get $c)))))\n");
    fprintf(f, "        (local.set $h (local.get $g)) (local.set $g (local.get $f)) (local.set $f (local.get $e))\n");
    fprintf(f, "        (local.set $e (i32.add (local.get $d) (local.get $t1)))\n");
    fprintf(f, "        (local.set $d (local.get $c)) (local.set $c (local.get $b)) (local.set $b (local.get $a))\n");
    fprintf(f, "        (local.set $a (i32.add (local.get $t1) (local.get $t2)))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $rl)))\n");
    // State += working vars.
    fprintf(f, "      (local.set $h0 (i32.add (local.get $h0) (local.get $a))) (local.set $h1 (i32.add (local.get $h1) (local.get $b)))\n");
    fprintf(f, "      (local.set $h2 (i32.add (local.get $h2) (local.get $c))) (local.set $h3 (i32.add (local.get $h3) (local.get $d)))\n");
    fprintf(f, "      (local.set $h4 (i32.add (local.get $h4) (local.get $e))) (local.set $h5 (i32.add (local.get $h5) (local.get $f)))\n");
    fprintf(f, "      (local.set $h6 (i32.add (local.get $h6) (local.get $g))) (local.set $h7 (i32.add (local.get $h7) (local.get $h)))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $bl)))\n");
    // Stash state back into $w[0..7] and emit 64 big-endian hex chars.
    fprintf(f, "    (i32.store offset=0 (local.get $w) (local.get $h0)) (i32.store offset=4 (local.get $w) (local.get $h1))\n");
    fprintf(f, "    (i32.store offset=8 (local.get $w) (local.get $h2)) (i32.store offset=12 (local.get $w) (local.get $h3))\n");
    fprintf(f, "    (i32.store offset=16 (local.get $w) (local.get $h4)) (i32.store offset=20 (local.get $w) (local.get $h5))\n");
    fprintf(f, "    (i32.store offset=24 (local.get $w) (local.get $h6)) (i32.store offset=28 (local.get $w) (local.get $h7))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.const 65)))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $od (loop $ol\n");
    fprintf(f, "      (br_if $od (i32.ge_u (local.get $i) (i32.const 8)))\n");
    fprintf(f, "      (local.set $x (i32.load (i32.add (local.get $w) (i32.shl (local.get $i) (i32.const 2)))))\n");
    fprintf(f, "      (local.set $t (i32.const 0))\n");
    fprintf(f, "      (block $nd (loop $nl\n");
    fprintf(f, "        (br_if $nd (i32.ge_u (local.get $t) (i32.const 8)))\n");
    fprintf(f, "        (i32.store8 (i32.add (local.get $out) (i32.add (i32.shl (local.get $i) (i32.const 3)) (local.get $t)))\n");
    fprintf(f, "          (call $teko_hexc (i32.and (i32.shr_u (local.get $x) (i32.sub (i32.const 28) (i32.shl (local.get $t) (i32.const 2)))) (i32.const 15))))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $nl)))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $ol)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 64)) (i32.const 0)) (local.get $out))\n");
}

// Phase 13 (legacy): native in-module MD5 (RFC 1321) — LEGACY/INSECURE, interop only. Same
// in-module no-host shape as SHA-256; NUL-terminated input -> 32-char lowercase hex digest.
// Little-endian throughout (i32.load reads LE natively; output bytes are LE). frontend lowers
// hash.md5(x) to OP_CALL_RUNTIME id 6. K + S tables cached in heap via a global pointer.
static void emit_wasm_md5_runtime(FILE* f) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391
    };
    static const uint8_t S[64] = {
        7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22, 5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
        4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23, 6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21
    };
    int i;

    fprintf(f, "  (global $md5_t (mut i32) (i32.const 0))\n");
    fprintf(f, "  (func $teko_md5_t (result i32) (local $p i32)\n");
    fprintf(f, "    (if (global.get $md5_t) (then (return (global.get $md5_t))))\n");
    fprintf(f, "    (local.set $p (call $teko_alloc (i32.const 512)))\n");
    for (i = 0; i < 64; ++i) fprintf(f, "    (i32.store offset=%d (local.get $p) (i32.const 0x%08x))\n", i * 4, K[i]);
    for (i = 0; i < 64; ++i) fprintf(f, "    (i32.store offset=%d (local.get $p) (i32.const %u))\n", 256 + i * 4, (unsigned)S[i]);
    fprintf(f, "    (global.set $md5_t (local.get $p)) (local.get $p))\n");

    // Raw core: writes the 16 little-endian digest bytes of MD5(in[0..len)) to out.
    fprintf(f, "  (func $teko_md5_raw (param $in i32) (param $len i32) (param $out i32)\n");
    fprintf(f, "    (local $nb i32) (local $msg i32) (local $padlen i32) (local $kp i32)\n");
    fprintf(f, "    (local $i i32) (local $t i32) (local $blk i32) (local $bitlen i32)\n");
    fprintf(f, "    (local $f i32) (local $g i32) (local $a i32) (local $b i32) (local $c i32) (local $d i32)\n");
    fprintf(f, "    (local $h0 i32) (local $h1 i32) (local $h2 i32) (local $h3 i32)\n");
    fprintf(f, "    (local.set $kp (call $teko_md5_t))\n");
    fprintf(f, "    (local.set $nb (i32.div_u (i32.add (local.get $len) (i32.const 72)) (i32.const 64)))\n");
    fprintf(f, "    (local.set $padlen (i32.mul (local.get $nb) (i32.const 64)))\n");
    fprintf(f, "    (local.set $msg (call $teko_alloc (local.get $padlen)))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $zd (loop $zl (br_if $zd (i32.ge_u (local.get $i) (local.get $padlen)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $msg) (local.get $i)) (i32.const 0))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $zl)))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $cd (loop $cl (br_if $cd (i32.ge_u (local.get $i) (local.get $len)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $msg) (local.get $i)) (i32.load8_u (i32.add (local.get $in) (local.get $i))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $cl)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (local.get $len)) (i32.const 0x80))\n");
    fprintf(f, "    (local.set $bitlen (i32.shl (local.get $len) (i32.const 3)))\n");
    // 64-bit little-endian length: low 32 bits at padlen-8, (len>>29) byte at padlen-5.
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 8))) (i32.and (local.get $bitlen) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 7))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 8)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 6))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 16)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 5))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 24)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 4))) (i32.and (i32.shr_u (local.get $len) (i32.const 29)) (i32.const 255)))\n");
    fprintf(f, "    (local.set $h0 (i32.const 0x67452301)) (local.set $h1 (i32.const 0xefcdab89))\n");
    fprintf(f, "    (local.set $h2 (i32.const 0x98badcfe)) (local.set $h3 (i32.const 0x10325476))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $bd (loop $bl (br_if $bd (i32.ge_u (local.get $i) (local.get $nb)))\n");
    fprintf(f, "      (local.set $blk (i32.add (local.get $msg) (i32.mul (local.get $i) (i32.const 64))))\n");
    fprintf(f, "      (local.set $a (local.get $h0)) (local.set $b (local.get $h1)) (local.set $c (local.get $h2)) (local.set $d (local.get $h3))\n");
    fprintf(f, "      (local.set $t (i32.const 0))\n");
    fprintf(f, "      (block $rd (loop $rl (br_if $rd (i32.ge_u (local.get $t) (i32.const 64)))\n");
    fprintf(f, "        (if (i32.lt_u (local.get $t) (i32.const 16))\n");
    fprintf(f, "          (then (local.set $f (i32.or (i32.and (local.get $b) (local.get $c)) (i32.and (i32.xor (local.get $b) (i32.const -1)) (local.get $d)))) (local.set $g (local.get $t)))\n");
    fprintf(f, "          (else (if (i32.lt_u (local.get $t) (i32.const 32))\n");
    fprintf(f, "            (then (local.set $f (i32.or (i32.and (local.get $d) (local.get $b)) (i32.and (i32.xor (local.get $d) (i32.const -1)) (local.get $c)))) (local.set $g (i32.and (i32.add (i32.mul (local.get $t) (i32.const 5)) (i32.const 1)) (i32.const 15))))\n");
    fprintf(f, "            (else (if (i32.lt_u (local.get $t) (i32.const 48))\n");
    fprintf(f, "              (then (local.set $f (i32.xor (i32.xor (local.get $b) (local.get $c)) (local.get $d))) (local.set $g (i32.and (i32.add (i32.mul (local.get $t) (i32.const 3)) (i32.const 5)) (i32.const 15))))\n");
    fprintf(f, "              (else (local.set $f (i32.xor (local.get $c) (i32.or (local.get $b) (i32.xor (local.get $d) (i32.const -1))))) (local.set $g (i32.and (i32.mul (local.get $t) (i32.const 7)) (i32.const 15)))))))))\n");
    fprintf(f, "        (local.set $f (i32.add (i32.add (i32.add (local.get $f) (local.get $a))\n");
    fprintf(f, "          (i32.load (i32.add (local.get $kp) (i32.shl (local.get $t) (i32.const 2)))))\n");
    fprintf(f, "          (i32.load (i32.add (local.get $blk) (i32.shl (local.get $g) (i32.const 2))))))\n");
    fprintf(f, "        (local.set $a (local.get $d)) (local.set $d (local.get $c)) (local.set $c (local.get $b))\n");
    fprintf(f, "        (local.set $b (i32.add (local.get $b) (i32.rotl (local.get $f) (i32.load (i32.add (local.get $kp) (i32.add (i32.const 256) (i32.shl (local.get $t) (i32.const 2))))))))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $rl)))\n");
    fprintf(f, "      (local.set $h0 (i32.add (local.get $h0) (local.get $a))) (local.set $h1 (i32.add (local.get $h1) (local.get $b)))\n");
    fprintf(f, "      (local.set $h2 (i32.add (local.get $h2) (local.get $c))) (local.set $h3 (i32.add (local.get $h3) (local.get $d)))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $bl)))\n");
    // Write the 16-byte little-endian digest to the caller's out buffer (store words LE).
    fprintf(f, "    (i32.store offset=0 (local.get $out) (local.get $h0)) (i32.store offset=4 (local.get $out) (local.get $h1))\n");
    fprintf(f, "    (i32.store offset=8 (local.get $out) (local.get $h2)) (i32.store offset=12 (local.get $out) (local.get $h3)))\n");
    // Hex wrapper: NUL-terminated input -> 32-char hex string.
    fprintf(f, "  (func $teko_md5_hex (export \"teko_md5_hex\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $dig i32) (local $out i32) (local $i i32) (local $x i32)\n");
    fprintf(f, "    (local.set $dig (call $teko_alloc (i32.const 16)))\n");
    fprintf(f, "    (call $teko_md5_raw (local.get $in) (call $teko_strlen (local.get $in)) (local.get $dig))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.const 33)))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $od (loop $ol (br_if $od (i32.ge_u (local.get $i) (i32.const 16)))\n");
    fprintf(f, "      (local.set $x (i32.load8_u (i32.add (local.get $dig) (local.get $i))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.shl (local.get $i) (i32.const 1))) (call $teko_hexc (i32.shr_u (local.get $x) (i32.const 4))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.add (i32.shl (local.get $i) (i32.const 1)) (i32.const 1))) (call $teko_hexc (i32.and (local.get $x) (i32.const 15))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $ol)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 32)) (i32.const 0)) (local.get $out))\n");
}

// Phase 13 (legacy): native in-module SHA-1 (FIPS 180) — LEGACY/INSECURE, interop only.
// Big-endian (like SHA-256); NUL-terminated input -> 40-char hex. frontend lowers
// hash.sha1(x) to OP_CALL_RUNTIME id 7.
static void emit_wasm_sha1_runtime(FILE* f) {
    // Raw core: writes the 20 big-endian digest bytes of SHA-1(in[0..len)) to out (binary,
    // length-based — usable for UUID v5 whose input contains NUL bytes).
    fprintf(f, "  (func $teko_sha1_raw (param $in i32) (param $len i32) (param $out i32)\n");
    fprintf(f, "    (local $nb i32) (local $msg i32) (local $padlen i32) (local $w i32)\n");
    fprintf(f, "    (local $i i32) (local $t i32) (local $blk i32) (local $bitlen i32)\n");
    fprintf(f, "    (local $f i32) (local $k i32) (local $tmp i32) (local $a i32) (local $b i32) (local $c i32) (local $d i32) (local $e i32)\n");
    fprintf(f, "    (local $h0 i32) (local $h1 i32) (local $h2 i32) (local $h3 i32) (local $h4 i32)\n");
    fprintf(f, "    (local.set $nb (i32.div_u (i32.add (local.get $len) (i32.const 72)) (i32.const 64)))\n");
    fprintf(f, "    (local.set $padlen (i32.mul (local.get $nb) (i32.const 64)))\n");
    fprintf(f, "    (local.set $msg (call $teko_alloc (local.get $padlen)))\n");
    fprintf(f, "    (local.set $w (call $teko_alloc (i32.const 320)))\n"); // 80 words
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $zd (loop $zl (br_if $zd (i32.ge_u (local.get $i) (local.get $padlen)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $msg) (local.get $i)) (i32.const 0))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $zl)))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $cd (loop $cl (br_if $cd (i32.ge_u (local.get $i) (local.get $len)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $msg) (local.get $i)) (i32.load8_u (i32.add (local.get $in) (local.get $i))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $cl)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (local.get $len)) (i32.const 0x80))\n");
    fprintf(f, "    (local.set $bitlen (i32.shl (local.get $len) (i32.const 3)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 5))) (i32.and (i32.shr_u (local.get $len) (i32.const 29)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 4))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 24)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 3))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 16)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 2))) (i32.and (i32.shr_u (local.get $bitlen) (i32.const 8)) (i32.const 255)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $msg) (i32.sub (local.get $padlen) (i32.const 1))) (i32.and (local.get $bitlen) (i32.const 255)))\n");
    fprintf(f, "    (local.set $h0 (i32.const 0x67452301)) (local.set $h1 (i32.const 0xefcdab89)) (local.set $h2 (i32.const 0x98badcfe))\n");
    fprintf(f, "    (local.set $h3 (i32.const 0x10325476)) (local.set $h4 (i32.const 0xc3d2e1f0))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $bd (loop $bl (br_if $bd (i32.ge_u (local.get $i) (local.get $nb)))\n");
    fprintf(f, "      (local.set $blk (i32.add (local.get $msg) (i32.mul (local.get $i) (i32.const 64))))\n");
    fprintf(f, "      (local.set $t (i32.const 0))\n");
    fprintf(f, "      (block $wd (loop $wl (br_if $wd (i32.ge_u (local.get $t) (i32.const 16)))\n");
    fprintf(f, "        (i32.store (i32.add (local.get $w) (i32.shl (local.get $t) (i32.const 2)))\n");
    fprintf(f, "          (i32.or (i32.or (i32.shl (i32.load8_u (i32.add (local.get $blk) (i32.shl (local.get $t) (i32.const 2)))) (i32.const 24))\n");
    fprintf(f, "                          (i32.shl (i32.load8_u (i32.add (local.get $blk) (i32.add (i32.shl (local.get $t) (i32.const 2)) (i32.const 1)))) (i32.const 16)))\n");
    fprintf(f, "                  (i32.or (i32.shl (i32.load8_u (i32.add (local.get $blk) (i32.add (i32.shl (local.get $t) (i32.const 2)) (i32.const 2)))) (i32.const 8))\n");
    fprintf(f, "                          (i32.load8_u (i32.add (local.get $blk) (i32.add (i32.shl (local.get $t) (i32.const 2)) (i32.const 3)))))))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $wl)))\n");
    fprintf(f, "      (local.set $t (i32.const 16))\n");
    fprintf(f, "      (block $sd (loop $sl (br_if $sd (i32.ge_u (local.get $t) (i32.const 80)))\n");
    fprintf(f, "        (i32.store (i32.add (local.get $w) (i32.shl (local.get $t) (i32.const 2)))\n");
    fprintf(f, "          (i32.rotl (i32.xor (i32.xor (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 3)) (i32.const 2))))\n");
    fprintf(f, "                                      (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 8)) (i32.const 2)))))\n");
    fprintf(f, "                              (i32.xor (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 14)) (i32.const 2))))\n");
    fprintf(f, "                                       (i32.load (i32.add (local.get $w) (i32.shl (i32.sub (local.get $t) (i32.const 16)) (i32.const 2)))))) (i32.const 1)))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $sl)))\n");
    fprintf(f, "      (local.set $a (local.get $h0)) (local.set $b (local.get $h1)) (local.set $c (local.get $h2)) (local.set $d (local.get $h3)) (local.set $e (local.get $h4))\n");
    fprintf(f, "      (local.set $t (i32.const 0))\n");
    fprintf(f, "      (block $rd (loop $rl (br_if $rd (i32.ge_u (local.get $t) (i32.const 80)))\n");
    fprintf(f, "        (if (i32.lt_u (local.get $t) (i32.const 20))\n");
    fprintf(f, "          (then (local.set $f (i32.or (i32.and (local.get $b) (local.get $c)) (i32.and (i32.xor (local.get $b) (i32.const -1)) (local.get $d)))) (local.set $k (i32.const 0x5a827999)))\n");
    fprintf(f, "          (else (if (i32.lt_u (local.get $t) (i32.const 40))\n");
    fprintf(f, "            (then (local.set $f (i32.xor (i32.xor (local.get $b) (local.get $c)) (local.get $d))) (local.set $k (i32.const 0x6ed9eba1)))\n");
    fprintf(f, "            (else (if (i32.lt_u (local.get $t) (i32.const 60))\n");
    fprintf(f, "              (then (local.set $f (i32.or (i32.or (i32.and (local.get $b) (local.get $c)) (i32.and (local.get $b) (local.get $d))) (i32.and (local.get $c) (local.get $d)))) (local.set $k (i32.const 0x8f1bbcdc)))\n");
    fprintf(f, "              (else (local.set $f (i32.xor (i32.xor (local.get $b) (local.get $c)) (local.get $d))) (local.set $k (i32.const 0xca62c1d6))))))))\n");
    fprintf(f, "        (local.set $tmp (i32.add (i32.add (i32.add (i32.add (i32.rotl (local.get $a) (i32.const 5)) (local.get $f)) (local.get $e)) (local.get $k))\n");
    fprintf(f, "          (i32.load (i32.add (local.get $w) (i32.shl (local.get $t) (i32.const 2))))))\n");
    fprintf(f, "        (local.set $e (local.get $d)) (local.set $d (local.get $c)) (local.set $c (i32.rotl (local.get $b) (i32.const 30))) (local.set $b (local.get $a)) (local.set $a (local.get $tmp))\n");
    fprintf(f, "        (local.set $t (i32.add (local.get $t) (i32.const 1))) (br $rl)))\n");
    fprintf(f, "      (local.set $h0 (i32.add (local.get $h0) (local.get $a))) (local.set $h1 (i32.add (local.get $h1) (local.get $b))) (local.set $h2 (i32.add (local.get $h2) (local.get $c)))\n");
    fprintf(f, "      (local.set $h3 (i32.add (local.get $h3) (local.get $d))) (local.set $h4 (i32.add (local.get $h4) (local.get $e)))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $bl)))\n");
    // Write the 20-byte big-endian digest to the caller's out buffer.
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 0)) (i32.shr_u (local.get $h0) (i32.const 24)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 1)) (i32.shr_u (local.get $h0) (i32.const 16)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 2)) (i32.shr_u (local.get $h0) (i32.const 8)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 3)) (local.get $h0))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 4)) (i32.shr_u (local.get $h1) (i32.const 24)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 5)) (i32.shr_u (local.get $h1) (i32.const 16)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 6)) (i32.shr_u (local.get $h1) (i32.const 8)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 7)) (local.get $h1))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 8)) (i32.shr_u (local.get $h2) (i32.const 24)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 9)) (i32.shr_u (local.get $h2) (i32.const 16)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 10)) (i32.shr_u (local.get $h2) (i32.const 8)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 11)) (local.get $h2))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 12)) (i32.shr_u (local.get $h3) (i32.const 24)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 13)) (i32.shr_u (local.get $h3) (i32.const 16)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 14)) (i32.shr_u (local.get $h3) (i32.const 8)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 15)) (local.get $h3))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 16)) (i32.shr_u (local.get $h4) (i32.const 24)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 17)) (i32.shr_u (local.get $h4) (i32.const 16)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 18)) (i32.shr_u (local.get $h4) (i32.const 8)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 19)) (local.get $h4)))\n");
    // Hex wrapper: NUL-terminated input -> 40-char hex string.
    fprintf(f, "  (func $teko_sha1_hex (export \"teko_sha1_hex\") (param $in i32) (result i32)\n");
    fprintf(f, "    (local $dig i32) (local $out i32) (local $i i32) (local $x i32)\n");
    fprintf(f, "    (local.set $dig (call $teko_alloc (i32.const 20)))\n");
    fprintf(f, "    (call $teko_sha1_raw (local.get $in) (call $teko_strlen (local.get $in)) (local.get $dig))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.const 41)))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $hd (loop $hl (br_if $hd (i32.ge_u (local.get $i) (i32.const 20)))\n");
    fprintf(f, "      (local.set $x (i32.load8_u (i32.add (local.get $dig) (local.get $i))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.shl (local.get $i) (i32.const 1))) (call $teko_hexc (i32.shr_u (local.get $x) (i32.const 4))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.add (i32.shl (local.get $i) (i32.const 1)) (i32.const 1))) (call $teko_hexc (i32.and (local.get $x) (i32.const 15))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $hl)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 40)) (i32.const 0)) (local.get $out))\n");
}

// Phase 13: native in-module UUID v3/v5 (RFC 4122) — name-based, DNS namespace. Builds
// (namespace||name) and hashes it with the length-based MD5/SHA-1 raw cores (the input has
// NUL bytes), stamps version+variant, and formats canonically. frontend lowers uuid.v3(name)
// -> id 8, uuid.v5(name) -> id 9. v4/v7 (entropy/time) stay C-runtime-only (host import TBD).
static void emit_wasm_uuid_runtime(FILE* f) {
    static const uint8_t NS_DNS[16] = {
        0x6b,0xa7,0xb8,0x10,0x9d,0xad,0x11,0xd1,0x80,0xb4,0x00,0xc0,0x4f,0xd4,0x30,0xc8
    };
    int i;

    // Format 16 bytes at $b into a fresh canonical "8-4-4-4-12" lowercase string (37 bytes).
    fprintf(f, "  (func $teko_uuid_fmt (param $b i32) (result i32)\n");
    fprintf(f, "    (local $out i32) (local $i i32) (local $pos i32) (local $x i32)\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.const 37)))\n");
    fprintf(f, "    (block $od (loop $ol (br_if $od (i32.ge_u (local.get $i) (i32.const 16)))\n");
    fprintf(f, "      (local.set $x (i32.load8_u (i32.add (local.get $b) (local.get $i))))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $pos)) (call $teko_hexc (i32.shr_u (local.get $x) (i32.const 4)))) (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $pos)) (call $teko_hexc (i32.and (local.get $x) (i32.const 15)))) (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    fprintf(f, "      (if (i32.or (i32.or (i32.eq (local.get $i) (i32.const 3)) (i32.eq (local.get $i) (i32.const 5))) (i32.or (i32.eq (local.get $i) (i32.const 7)) (i32.eq (local.get $i) (i32.const 9))))\n");
    fprintf(f, "        (then (i32.store8 (i32.add (local.get $out) (local.get $pos)) (i32.const 45)) (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $ol)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 36)) (i32.const 0)) (local.get $out))\n");

    // v5 = SHA-1(DNS-ns || name) truncated to 16, version 5 / variant 10xx.
    fprintf(f, "  (func $teko_uuid_v5 (export \"teko_uuid_v5\") (param $name i32) (result i32)\n");
    fprintf(f, "    (local $nl i32) (local $buf i32) (local $dig i32) (local $i i32)\n");
    fprintf(f, "    (local.set $nl (call $teko_strlen (local.get $name)))\n");
    fprintf(f, "    (local.set $buf (call $teko_alloc (i32.add (i32.const 16) (local.get $nl))))\n");
    for (i = 0; i < 16; ++i) fprintf(f, "    (i32.store8 (i32.add (local.get $buf) (i32.const %d)) (i32.const %u))\n", i, (unsigned)NS_DNS[i]);
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $cd (loop $cl (br_if $cd (i32.ge_u (local.get $i) (local.get $nl)))\n");
    fprintf(f, "      (i32.store8 (i32.add (i32.add (local.get $buf) (i32.const 16)) (local.get $i)) (i32.load8_u (i32.add (local.get $name) (local.get $i))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $cl)))\n");
    fprintf(f, "    (local.set $dig (call $teko_alloc (i32.const 20)))\n");
    fprintf(f, "    (call $teko_sha1_raw (local.get $buf) (i32.add (i32.const 16) (local.get $nl)) (local.get $dig))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $dig) (i32.const 6)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $dig) (i32.const 6))) (i32.const 15)) (i32.const 0x50)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $dig) (i32.const 8)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $dig) (i32.const 8))) (i32.const 0x3f)) (i32.const 0x80)))\n");
    fprintf(f, "    (call $teko_uuid_fmt (local.get $dig)))\n");

    // v3 = MD5(DNS-ns || name), version 3 / variant 10xx.
    fprintf(f, "  (func $teko_uuid_v3 (export \"teko_uuid_v3\") (param $name i32) (result i32)\n");
    fprintf(f, "    (local $nl i32) (local $buf i32) (local $dig i32) (local $i i32)\n");
    fprintf(f, "    (local.set $nl (call $teko_strlen (local.get $name)))\n");
    fprintf(f, "    (local.set $buf (call $teko_alloc (i32.add (i32.const 16) (local.get $nl))))\n");
    for (i = 0; i < 16; ++i) fprintf(f, "    (i32.store8 (i32.add (local.get $buf) (i32.const %d)) (i32.const %u))\n", i, (unsigned)NS_DNS[i]);
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $cd (loop $cl (br_if $cd (i32.ge_u (local.get $i) (local.get $nl)))\n");
    fprintf(f, "      (i32.store8 (i32.add (i32.add (local.get $buf) (i32.const 16)) (local.get $i)) (i32.load8_u (i32.add (local.get $name) (local.get $i))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $cl)))\n");
    fprintf(f, "    (local.set $dig (call $teko_alloc (i32.const 16)))\n");
    fprintf(f, "    (call $teko_md5_raw (local.get $buf) (i32.add (i32.const 16) (local.get $nl)) (local.get $dig))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $dig) (i32.const 6)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $dig) (i32.const 6))) (i32.const 15)) (i32.const 0x30)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $dig) (i32.const 8)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $dig) (i32.const 8))) (i32.const 0x3f)) (i32.const 0x80)))\n");
    fprintf(f, "    (call $teko_uuid_fmt (local.get $dig)))\n");
}

// Phase 13 (Sub-phase C): CSPRNG on the WASM surface via a host entropy import.
// frontend lowers `random.bytes(n)` -> OP_CALL_RUNTIME id 41; the accumulator holds n.
// We can't run the constant-time C CSPRNG in-module yet (that is the deferred "compile the
// C crypto runtime -> wasm32" step), so entropy comes from the embedder through the
// `env.teko_random(ptr, len)` import (Node: crypto.randomFillSync; browser: crypto
// .getRandomValues) — the SAME host-import shape used for dom.*. We allocate n bytes, ask the
// host to fill them, then lowercase-hex-encode in-module (no dependency on the codec runtime,
// which a random-only program does not emit). Returns a NUL-terminated hex string pointer.
static void emit_wasm_random_runtime(FILE* f) {
    fprintf(f, "  (func $teko_random_hex (param $n i32) (result i32)\n");
    fprintf(f, "    (local $buf i32) (local $out i32) (local $i i32) (local $b i32) (local $h i32)\n");
    fprintf(f, "    (if (i32.le_s (local.get $n) (i32.const 0)) (then (return (i32.const 0))))\n");
    fprintf(f, "    (local.set $buf (call $teko_alloc (local.get $n)))\n");
    fprintf(f, "    (call $teko_random (local.get $buf) (local.get $n))\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.add (i32.mul (local.get $n) (i32.const 2)) (i32.const 1))))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $done (loop $loop\n");
    fprintf(f, "      (br_if $done (i32.ge_u (local.get $i) (local.get $n)))\n");
    fprintf(f, "      (local.set $b (i32.load8_u (i32.add (local.get $buf) (local.get $i))))\n");
    fprintf(f, "      (local.set $h (i32.shr_u (local.get $b) (i32.const 4)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (i32.mul (local.get $i) (i32.const 2)))\n");
    fprintf(f, "        (i32.add (local.get $h) (select (i32.const 48) (i32.const 87) (i32.lt_u (local.get $h) (i32.const 10)))))\n");
    fprintf(f, "      (local.set $h (i32.and (local.get $b) (i32.const 15)))\n");
    fprintf(f, "      (i32.store8 (i32.add (i32.add (local.get $out) (i32.mul (local.get $i) (i32.const 2))) (i32.const 1))\n");
    fprintf(f, "        (i32.add (local.get $h) (select (i32.const 48) (i32.const 87) (i32.lt_u (local.get $h) (i32.const 10)))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1)))\n");
    fprintf(f, "      (br $loop)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.mul (local.get $n) (i32.const 2))) (i32.const 0))\n");
    fprintf(f, "    (local.get $out))\n");
}

// Phase 13 (Sub-phase C): self-contained uuid.v4 / uuid.v7 on the WASM surface. Entropy comes
// from the host import env.teko_random; v7 also reads a 48-bit Unix-ms timestamp from
// env.teko_now (i64). Output is the canonical lowercase "8-4-4-4-12" string. Depends only on
// $teko_alloc + the host imports (its own inline select-based hex formatter — no codec/hash
// runtime), so a uuid-only module stays lean. Frontend lowers uuid.v4()/v7() -> id 42/43; the
// (ignored) $w0 arg keeps the funcs compatible with the accumulator dispatch.
static void emit_wasm_uuid_rng_runtime(FILE* f) {
    fprintf(f, "  (func $teko_uuid_rng_fmt (param $b i32) (result i32)\n");
    fprintf(f, "    (local $out i32) (local $i i32) (local $pos i32) (local $x i32) (local $nb i32)\n");
    fprintf(f, "    (local.set $out (call $teko_alloc (i32.const 37)))\n");
    fprintf(f, "    (block $od (loop $ol (br_if $od (i32.ge_u (local.get $i) (i32.const 16)))\n");
    fprintf(f, "      (local.set $x (i32.load8_u (i32.add (local.get $b) (local.get $i))))\n");
    fprintf(f, "      (local.set $nb (i32.shr_u (local.get $x) (i32.const 4)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $pos))\n");
    fprintf(f, "        (i32.add (local.get $nb) (select (i32.const 48) (i32.const 87) (i32.lt_u (local.get $nb) (i32.const 10)))))\n");
    fprintf(f, "      (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    fprintf(f, "      (local.set $nb (i32.and (local.get $x) (i32.const 15)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $out) (local.get $pos))\n");
    fprintf(f, "        (i32.add (local.get $nb) (select (i32.const 48) (i32.const 87) (i32.lt_u (local.get $nb) (i32.const 10)))))\n");
    fprintf(f, "      (local.set $pos (i32.add (local.get $pos) (i32.const 1)))\n");
    fprintf(f, "      (if (i32.or (i32.or (i32.eq (local.get $i) (i32.const 3)) (i32.eq (local.get $i) (i32.const 5)))\n");
    fprintf(f, "                  (i32.or (i32.eq (local.get $i) (i32.const 7)) (i32.eq (local.get $i) (i32.const 9))))\n");
    fprintf(f, "        (then (i32.store8 (i32.add (local.get $out) (local.get $pos)) (i32.const 45)) (local.set $pos (i32.add (local.get $pos) (i32.const 1)))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $ol)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $out) (i32.const 36)) (i32.const 0)) (local.get $out))\n");

    fprintf(f, "  (func $teko_uuid_v4 (export \"teko_uuid_v4\") (param $ignore i32) (result i32) (local $b i32)\n");
    fprintf(f, "    (local.set $b (call $teko_alloc (i32.const 16)))\n");
    fprintf(f, "    (call $teko_random (local.get $b) (i32.const 16))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $b) (i32.const 6)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $b) (i32.const 6))) (i32.const 15)) (i32.const 0x40)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $b) (i32.const 8)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $b) (i32.const 8))) (i32.const 0x3f)) (i32.const 0x80)))\n");
    fprintf(f, "    (call $teko_uuid_rng_fmt (local.get $b)))\n");

    fprintf(f, "  (func $teko_uuid_v7 (export \"teko_uuid_v7\") (param $ignore i32) (result i32) (local $b i32) (local $ms i64) (local $i i32)\n");
    fprintf(f, "    (local.set $b (call $teko_alloc (i32.const 16)))\n");
    fprintf(f, "    (call $teko_random (local.get $b) (i32.const 16))\n");
    fprintf(f, "    (local.set $ms (call $teko_now))\n");
    fprintf(f, "    (local.set $i (i32.const 0))\n");
    fprintf(f, "    (block $td (loop $tl (br_if $td (i32.ge_u (local.get $i) (i32.const 6)))\n");
    fprintf(f, "      (i32.store8 (i32.add (local.get $b) (local.get $i))\n");
    fprintf(f, "        (i32.wrap_i64 (i64.and (i64.shr_u (local.get $ms) (i64.extend_i32_u (i32.mul (i32.sub (i32.const 5) (local.get $i)) (i32.const 8)))) (i64.const 0xff))))\n");
    fprintf(f, "      (local.set $i (i32.add (local.get $i) (i32.const 1))) (br $tl)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $b) (i32.const 6)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $b) (i32.const 6))) (i32.const 15)) (i32.const 0x70)))\n");
    fprintf(f, "    (i32.store8 (i32.add (local.get $b) (i32.const 8)) (i32.or (i32.and (i32.load8_u (i32.add (local.get $b) (i32.const 8))) (i32.const 0x3f)) (i32.const 0x80)))\n");
    fprintf(f, "    (call $teko_uuid_rng_fmt (local.get $b)))\n");
}

// The non-suspending half of a channel receive: read buf[head] -> $w0, advance
// head = (head+1) % cap. Channel base is in $cp.
static void emit_wasm_chan_read(FILE* f) {
    fprintf(f, "    local.get $cp\n");
    fprintf(f, "    local.get $cp\n    i32.load offset=0\n    i32.const 4\n    i32.mul\n    i32.add\n"); // cp + head*4
    fprintf(f, "    i32.load offset=12\n    local.set $w0\n");                                            // $w0 = mem[cp+head*4+12]
    fprintf(f, "    local.get $cp\n");
    fprintf(f, "    local.get $cp\n    i32.load offset=0\n    i32.const 1\n    i32.add\n");
    fprintf(f, "    local.get $cp\n    i32.load offset=8\n    i32.rem_u\n");
    fprintf(f, "    i32.store offset=0\n");                                                               // head = (head+1) % cap
}

// ===========================================================================
// Layer B — `--target=...-wasm-threads`: real multicore. The module imports a
// SHARED memory and channels use the atomics proposal (memory.atomic.wait32 /
// .notify + i32.atomic.*) instead of the cooperative scheduler. SPAWN delegates
// to a host `teko_rt.spawn(fn_index, arg)` that starts a Web Worker /
// worker_threads thread; that thread re-instantiates the same module against the
// shared memory and calls the exported `teko_invoke` dispatcher. Green threads
// are 1:1 OS threads here (opt-in parallelism on top of Layer A, not a
// replacement). Channel cell layout: [0]=ready flag, [4]=value.
// ===========================================================================
static void emit_wasm_threads(MetalContext* ctx, OpCode op, int32_t arg) {
    FILE* f = ctx->file;
    switch (op) {
        case OP_PROLOG:
            fprintf(f, "(module\n");
            fprintf(f, "  ;; --- Target: WebAssembly Text Format (wasm-threads / Layer B, Phase 10.4) ---\n");
            fprintf(f, "  (import \"env\" \"memory\" (memory 1 1 shared))\n");
            fprintf(f, "  (export \"memory\" (memory 0))\n");
            fprintf(f, "  (import \"teko_rt\" \"spawn\" (func $teko_spawn (param i32 i32)))\n");
            fprintf(f, "  (global $arena_sp (mut i32) (i32.const 2048))\n");
            fprintf(f, "  (type $task (func (param i32)))\n");
            fprintf(f, "  (func $main (result i32)\n");
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32) (local $spins i32)\n");
            ctx->wasm_open = 1;
            break;

        case OP_HALT:
            fprintf(f, "    ;; [WASM Halt]: result is $w0\n");
            break;
        case OP_ICONST:
            fprintf(f, "    i32.const %d\n    local.set $w0\n", arg);
            break;
        case OP_SCONST:
            fprintf(f, "    i32.const %d\n    local.set $w0\n", arg * 32);
            break;
        case OP_STORE:
            fprintf(f, "    local.get $w0\n    local.set $w1\n");
            break;
        case OP_LOAD:
            fprintf(f, "    local.get $w1\n    local.set $w0\n");
            break;
        case OP_ADD:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.add\n    local.set $w0\n");
            break;
        case OP_SUB:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.sub\n    local.set $w0\n");
            break;
        case OP_MUL:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.mul\n    local.set $w0\n");
            break;
        case OP_DIV:
            fprintf(f, "    local.get $w1\n    i32.eqz\n    if (result i32)\n      i32.const -1\n    else\n");
            fprintf(f, "      local.get $w0\n      local.get $w1\n      i32.div_s\n    end\n    local.set $w0\n");
            break;
        case OP_ARENA_PUSH:
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.add\n    global.set $arena_sp\n");
            break;
        case OP_ARENA_POP:
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.sub\n    global.set $arena_sp\n");
            break;

        case OP_CHAN_INIT:
            // Channel cell {flag@0, value@4} in shared memory; flag starts empty.
            fprintf(f, "    ;; [WASM-threads Channel Init]: atomic cell in shared memory\n");
            fprintf(f, "    global.get $arena_sp\n    local.set $cp\n");
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.atomic.store offset=0\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 8\n    i32.add\n    global.set $arena_sp\n");
            break;

        case OP_CHAN_PUT:
            // Atomically publish the value, then wake one waiter.
            fprintf(f, "    ;; [WASM-threads Channel Put]: atomic store + notify\n");
            fprintf(f, "    local.get $cp\n    local.get $w0\n    i32.atomic.store offset=4\n");
            fprintf(f, "    local.get $cp\n    i32.const 1\n    i32.atomic.store offset=0\n");
            fprintf(f, "    local.get $cp\n    i32.const 1\n    memory.atomic.notify offset=0\n    drop\n");
            break;

        case OP_CHAN_GET:
            // Busy-poll the channel flag with an ATOMIC load until the producer
            // sets it, then read the value. This deliberately does NOT use
            // memory.atomic.wait32/notify: a cross-instance notify was observed to
            // never reach the waiter on some runtimes (the wait blocks forever).
            // Cross-thread visibility of the shared memory is guaranteed by the
            // memory model, so the atomic load is certain to observe the store —
            // no notify required. (A production scheduler would back off; this is
            // the correctness-first form for the cooperative-over-threads MVP.)
            // Bounded: spin at most ~2e9 iterations (~1-2s of CPU) so a producer
            // that never publishes traps with `unreachable` instead of spinning
            // forever / pegging a core indefinitely. The harness watchdog and the
            // job timeout-minutes are outer backstops; this makes the module itself
            // terminate with a clear hard error.
            fprintf(f, "    ;; [WASM-threads Channel Get]: notify-free atomic busy-poll (bounded) on the flag\n");
            fprintf(f, "    i32.const 0\n    local.set $spins\n");
            fprintf(f, "    (block $ready\n      (loop $spin\n");
            fprintf(f, "        (br_if $ready (i32.eq (i32.atomic.load offset=0 (local.get $cp)) (i32.const 1)))\n");
            fprintf(f, "        (local.set $spins (i32.add (local.get $spins) (i32.const 1)))\n");
            fprintf(f, "        (if (i32.gt_u (local.get $spins) (i32.const 2000000000)) (then unreachable))\n");
            fprintf(f, "        (br $spin)))\n");
            fprintf(f, "    local.get $cp\n    i32.atomic.load offset=4\n    local.set $w0\n");
            break;

        case OP_SPAWN_ASYNC:
            // Hand off to the host: start a real Worker for routine $w0 with arg $cp.
            fprintf(f, "    ;; [WASM-threads Spawn]: host starts a Worker (fn=$w0, arg=$cp)\n");
            fprintf(f, "    local.get $w0\n    local.get $cp\n    call $teko_spawn\n");
            break;

        case OP_AWAIT_INTENT:
            fprintf(f, "    ;; [WASM-threads Await]: parallelism is real; no cooperative yield\n");
            break;

        case OP_FUNC_BEGIN:
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
            }
            fprintf(f, "  (func $routine_%d (param $arg i32)\n", arg);
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32) (local $spins i32)\n");
            fprintf(f, "    local.get $arg\n    local.set $cp\n");
            ctx->wasm_open = 2;
            if (ctx->wasm_routine_count < 64) ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            ctx->wasm_routine_count++;
            break;

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
                ctx->wasm_open = 0;
            }
            break;

        case OP_RETURN:
        case OP_EPILOG: {
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "  )\n");
            }
            ctx->wasm_open = 0;
            int n = ctx->wasm_routine_count;
            fprintf(f, "  (table %d funcref)\n", n > 0 ? n : 1);
            if (n > 0) {
                fprintf(f, "  (elem (i32.const 0)");
                for (int k = 0; k < n && k < 64; k++) fprintf(f, " $routine_%d", ctx->wasm_routine_ids[k]);
                fprintf(f, ")\n");
            }
            // Exported dispatcher so a host Worker can run a routine by table index.
            fprintf(f, "  (func $teko_invoke (export \"teko_invoke\") (param $fn i32) (param $arg i32)\n");
            fprintf(f, "    (call_indirect (type $task) (local.get $arg) (local.get $fn)))\n");
            fprintf(f, "  (export \"main\" (func $main))\n");
            fprintf(f, ")\n");
            break;
        }

        default:
            break;
    }
}

void emit_wasm_pure(MetalContext* ctx, OpCode op, int32_t arg) {
    if (!ctx || !ctx->file) return;
    // Layer B: opt-in real multicore via `--target=...-wasm-threads`.
    if (ctx->target.target_string[0] && strstr(ctx->target.target_string, "threads")) {
        emit_wasm_threads(ctx, op, arg);
        return;
    }
    FILE* f = ctx->file;

    switch (op) {
        // ====================================================================
        // 1. MODULE INIT + SCHEDULER RUNTIME + $main OPEN
        // ====================================================================
        case OP_PROLOG:
            fprintf(f, "(module\n");
            fprintf(f, "  ;; --- Target: WebAssembly Text Format (cooperative concurrency, Phase 10.3) ---\n");
            emit_wasm_imports(ctx);            // Phase 11 FFI host imports — must precede definitions
            // Phase 13 Sub-phase C host imports (must precede memory). Entropy is shared by
            // random.bytes and uuid.v4/v7; the time import is uuid.v7-only.
            if (ctx->wasm_emit_random || ctx->wasm_emit_uuid_rng) {
                fprintf(f, "  (import \"env\" \"teko_random\" (func $teko_random (param i32 i32)))\n");
            }
            if (ctx->wasm_emit_uuid_rng) {
                fprintf(f, "  (import \"env\" \"teko_now\" (func $teko_now (result i64)))\n");
            }
            // Phase 13 Sub-phase C (big step): import the compiled-C crypto reactor's
            // teko_rt_* entry points. The reactor (crypto.wasm) is a SECOND module the
            // host instantiates against the SAME linear memory; its bump heap lives above
            // Teko's [0..65536) region (link --global-base=65536), so the allocators never
            // alias. Declare an import per reactor-backed id (all imports must precede defs).
            if (ctx->wasm_emit_crypto_ext) {
                for (int id = 0; id <= 40; id++) {
                    int ar = 1;
                    const char* sym;
                    if (!wasm_is_crypto_ext_id(id)) continue;
                    sym = teko_native_runtime_symbol(id, &ar);
                    if (!sym) continue;
                    fprintf(f, "  (import \"crypto\" \"%s\" (func $crypto_%d", sym, id);
                    for (int p = 0; p < ar; p++) fprintf(f, " (param i32)");
                    fprintf(f, " (result i32)))\n");
                }
            }
            // Memory: module-owned by default; when the crypto reactor is in play it is
            // host-owned and SHARED (imported from env), so both modules address the same
            // bytes. Re-export it either way so harnesses can read results via exports.memory.
            if (ctx->wasm_emit_crypto_ext) {
                fprintf(f, "  (import \"env\" \"memory\" (memory 1))\n");
            } else {
                fprintf(f, "  (memory 1)\n");
            }
            fprintf(f, "  (export \"memory\" (memory 0))\n");
            fprintf(f, "  (global $arena_sp (mut i32) (i32.const 2048))\n");
            emit_wasm_scheduler_runtime(f);
            emit_wasm_heap_runtime(f);         // Phase 11 MVP-4: real freeing allocator
            if (ctx->wasm_emit_codecs || ctx->wasm_emit_hash) emit_wasm_runtime_common(f);
            if (ctx->wasm_emit_codecs) emit_wasm_codec_runtime(f); // Phase 12 P12-G
            if (ctx->wasm_emit_hash) {
                emit_wasm_hash_runtime(f);    // Phase 13.1 SHA-256
                emit_wasm_md5_runtime(f);     // Phase 13 legacy MD5
                emit_wasm_sha1_runtime(f);    // Phase 13 legacy SHA-1
                emit_wasm_uuid_runtime(f);    // Phase 13 UUID v3/v5 (name-based)
            }
            if (ctx->wasm_emit_random) emit_wasm_random_runtime(f); // Phase 13 Sub-phase C CSPRNG
            if (ctx->wasm_emit_uuid_rng) emit_wasm_uuid_rng_runtime(f); // Phase 13 Sub-phase C uuid v4/v7
            fprintf(f, "  (func $main (result i32)\n");
            // $w0 accumulator, $w1 scratch, $cp channel ptr, $a0..$a2 import-arg
            // staging slots (Phase 11 multi-param imports — see OP_SETARG).
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32)\n");
            fprintf(f, "    (local $a0 i32) (local $a1 i32) (local $a2 i32)\n");
            // Phase 12: named local variables ($v0..$v{n-1}) for `let`/`mut` bindings.
            for (int v = 0; v < ctx->wasm_local_count; v++) {
                fprintf(f, "    (local $v%d i32)\n", v);
            }
            ctx->wasm_open = 1;
            break;

        // ====================================================================
        // 2. LITERALS / MEMORY (accumulator model: results land in $w0)
        // ====================================================================
        case OP_HALT:
            fprintf(f, "    ;; [WASM Halt]: result is $w0, returned at function close\n");
            break;

        case OP_ICONST:
            fprintf(f, "    i32.const %d\n    local.set $w0\n", arg);
            break;

        case OP_SCONST:
            // $w0 = byte offset of the constant-pool string in linear memory. With a
            // real pool (Phase 11) this is the true packed offset; without one we keep
            // the legacy placeholder so pool-less programs still emit.
            if (arg >= 0 && arg < ctx->wasm_string_count) {
                fprintf(f, "    i32.const %d ;; &pool[%d] (\"%.16s\")\n    local.set $w0\n",
                        teko_wasm_string_offset(ctx, arg), arg, ctx->wasm_strings[arg]);
            } else {
                fprintf(f, "    i32.const %d ;; Offset of Constant Pool in Linear Memory\n    local.set $w0\n", arg * 32);
            }
            break;

        case OP_STORE:
            fprintf(f, "    local.get $w0\n    local.set $w1\n");
            break;

        case OP_LOAD:
            fprintf(f, "    local.get $w1\n    local.set $w0\n");
            break;

        // Phase 12 (P12-G): native base-encoding codec. $w0 = codec($w0).
        case OP_CALL_RUNTIME: {
            const char* fn = NULL;
            if (arg == 0) fn = "teko_base64_encode";
            else if (arg == 1) fn = "teko_base64_decode";
            else if (arg == 2) fn = "teko_hex_encode";
            else if (arg == 3) fn = "teko_hex_decode";
            else if (arg == 4) fn = "teko_sha256_hex"; // Phase 13.1
            else if (arg == 6) fn = "teko_md5_hex";    // Phase 13 legacy
            else if (arg == 7) fn = "teko_sha1_hex";   // Phase 13 legacy
            else if (arg == 8) fn = "teko_uuid_v3";    // Phase 13 UUID (name-based)
            else if (arg == 9) fn = "teko_uuid_v5";
            else if (arg == 41) fn = "teko_random_hex"; // Phase 13 Sub-phase C CSPRNG (host entropy)
            else if (arg == 42) fn = "teko_uuid_v4";    // Phase 13 Sub-phase C uuid v4 (host entropy)
            else if (arg == 43) fn = "teko_uuid_v7";    // Phase 13 Sub-phase C uuid v7 (host time+entropy)
            if (fn) {
                fprintf(f, "    local.get $w0\n    call $%s\n    local.set $w0\n", fn);
            } else if (wasm_is_crypto_ext_id(arg)) {
                // Reactor-backed crypto: call the imported teko_rt_* entry point. Multi-arg
                // ABI mirrors OP_CALL_IMPORT — args 0..n-2 come from the staging slots
                // $a0..$a(n-2) (set by OP_SETARG), the last from $w0; the result lands in
                // $w0. Stack-neutral: n pushes consumed by the call, one result popped.
                int ar = 1;
                teko_native_runtime_symbol(arg, &ar);
                for (int p = 0; p + 1 < ar; p++) fprintf(f, "    local.get $a%d\n", p);
                fprintf(f, "    local.get $w0\n    call $crypto_%d\n    local.set $w0\n", arg);
            } else {
                // Genuinely un-lowered runtime id (none remain in the crypto surface): trap
                // loudly rather than mis-call another runtime fn, so a token is never silently
                // wrong on the WASM surface.
                fprintf(f, "    unreachable ;; crypto runtime id %d not lowered to WASM\n", arg);
            }
            break;
        }

        // Phase 12: named local variables ($v0..$vN).
        case OP_STORE_LOCAL:
            fprintf(f, "    local.get $w0\n    local.set $v%d\n", arg);
            break;

        case OP_LOAD_LOCAL:
            fprintf(f, "    local.get $v%d\n    local.set $w0\n", arg);
            break;

        // ====================================================================
        // 3. ARITHMETIC ($w0 = $w0 <op> $w1)
        // ====================================================================
        case OP_ADD:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.add\n    local.set $w0\n");
            break;

        case OP_SUB:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.sub\n    local.set $w0\n");
            break;

        case OP_MUL:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.mul\n    local.set $w0\n");
            break;

        case OP_DIV:
            fprintf(f, "    local.get $w1\n    i32.eqz\n    if (result i32)\n");
            fprintf(f, "      i32.const -1\n    else\n");
            fprintf(f, "      local.get $w0\n      local.get $w1\n      i32.div_s\n    end\n");
            fprintf(f, "    local.set $w0\n");
            break;

        // Phase 12 (P12-E): integer modulo + comparisons ($w0 = $w0 <op> $w1).
        case OP_MOD: // guard divide-by-zero (→ 0), like OP_DIV
            fprintf(f, "    local.get $w1\n    i32.eqz\n    if (result i32)\n");
            fprintf(f, "      i32.const 0\n    else\n");
            fprintf(f, "      local.get $w0\n      local.get $w1\n      i32.rem_s\n    end\n");
            fprintf(f, "    local.set $w0\n");
            break;
        case OP_EQ:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.eq\n    local.set $w0\n");
            break;
        case OP_NE:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.ne\n    local.set $w0\n");
            break;
        case OP_LT:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.lt_s\n    local.set $w0\n");
            break;
        case OP_LE:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.le_s\n    local.set $w0\n");
            break;
        case OP_GT:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.gt_s\n    local.set $w0\n");
            break;
        case OP_GE:
            fprintf(f, "    local.get $w0\n    local.get $w1\n    i32.ge_s\n    local.set $w0\n");
            break;

        // ====================================================================
        // 4. NATIVE ARENA ALLOCATOR (O(1) bump)
        // ====================================================================
        case OP_ARENA_PUSH:
            fprintf(f, "    ;; --- [WASM Arena Push]: O(1) bump of a 1024-byte frame ---\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.add\n    global.set $arena_sp\n");
            break;

        case OP_ARENA_POP:
            fprintf(f, "    ;; --- [WASM Arena Pop]: O(1) reclaim of the 1024-byte frame ---\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 1024\n    i32.sub\n    global.set $arena_sp\n");
            break;

        // ====================================================================
        // 5. CHANNELS (in-module linear-memory ring buffers)
        //    Header (i32 cells): [0]=head [4]=tail [8]=cap, data slots at +12.
        //    Channel base lives in $cp.
        // ====================================================================
        case OP_CHAN_INIT:
            fprintf(f, "    ;; [WASM Channel Init]: ring buffer in linear memory (cap 8)\n");
            fprintf(f, "    global.get $arena_sp\n    local.set $cp\n");
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.store offset=0\n");
            fprintf(f, "    local.get $cp\n    i32.const 0\n    i32.store offset=4\n");
            fprintf(f, "    local.get $cp\n    i32.const 8\n    i32.store offset=8\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const 44\n    i32.add\n    global.set $arena_sp\n");
            break;

        case OP_CHAN_PUT:
            fprintf(f, "    ;; [WASM Channel Put]: non-blocking ring-buffer store at tail\n");
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n    i32.const 4\n    i32.mul\n    i32.add\n");
            fprintf(f, "    local.get $w0\n    i32.store offset=12\n");
            fprintf(f, "    local.get $cp\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=4\n    i32.const 1\n    i32.add\n");
            fprintf(f, "    local.get $cp\n    i32.load offset=8\n    i32.rem_u\n");
            fprintf(f, "    i32.store offset=4\n");
            break;

        case OP_CHAN_GET:
            if (ctx->wasm_open == 2) {
                // Inside a green thread: a true suspension point. Close the state
                // block for this segment; if the channel is empty, spill the live
                // registers and RETURN the resume state to the scheduler.
                int k = ++ctx->wasm_yield_idx;
                fprintf(f, "    )\n");                                          // close (block $s<k>)
                fprintf(f, "    ;; [WASM Channel Get @ yield %d]: suspend to scheduler if empty\n", k);
                fprintf(f, "    local.get $cp\n    i32.load offset=0\n");
                fprintf(f, "    local.get $cp\n    i32.load offset=4\n");
                fprintf(f, "    i32.eq\n    if\n");
                fprintf(f, "      local.get $frame\n      local.get $w0\n      i32.store offset=0\n"); // spill $w0
                fprintf(f, "      local.get $frame\n      local.get $w1\n      i32.store offset=4\n"); // spill $w1
                fprintf(f, "      i32.const %d\n      return\n    end\n", k);
                emit_wasm_chan_read(f);
            } else {
                // In $main (the root): block re-entrantly — drain the scheduler
                // once, then read. $main cannot suspend (nothing to return to).
                fprintf(f, "    ;; [WASM Channel Get]: root blocking receive (drains the scheduler)\n");
                fprintf(f, "    local.get $cp\n    i32.load offset=0\n");
                fprintf(f, "    local.get $cp\n    i32.load offset=4\n");
                fprintf(f, "    i32.eq\n    if\n      call $teko_sched_run\n    end\n");
                emit_wasm_chan_read(f);
            }
            break;

        // ====================================================================
        // 5b. FFI: call a host import (Phase 11)
        // ====================================================================
        case OP_SETARG:
            // Stage the accumulator into import-arg slot $a<arg> (Phase 11 MVP-2):
            // an N-param import is fed by N-1 OP_SETARGs (slots 0..N-2) plus $w0.
            fprintf(f, "    local.get $w0\n    local.set $a%d\n", arg);
            break;

        case OP_CALL_IMPORT: {
            // Call $import_<arg>. Accumulator ABI: params 0..np-2 come from the
            // staging slots $a0..$a(np-2) (set by OP_SETARG); the LAST param comes
            // from the accumulator $w0. A result (if any) lands back in $w0. This
            // keeps single-arg imports (MVP-1) as just `local.get $w0`.
            int np = (arg >= 0 && arg < ctx->wasm_import_count) ? ctx->wasm_imports[arg].n_params : 0;
            int hr = (arg >= 0 && arg < ctx->wasm_import_count) ? ctx->wasm_imports[arg].has_result : 0;
            fprintf(f, "    ;; [WASM FFI]: call import #%d (%d param%s)\n", arg, np, np == 1 ? "" : "s");
            for (int p = 0; p + 1 < np; p++) fprintf(f, "    local.get $a%d\n", p);
            if (np >= 1) fprintf(f, "    local.get $w0\n");
            fprintf(f, "    call $import_%d\n", arg);
            if (hr) fprintf(f, "    local.set $w0\n");
            break;
        }

        // ====================================================================
        // 6. CONCURRENCY: real cooperative spawn + yield (no host runtime)
        // ====================================================================
        case OP_SPAWN_ASYNC:
            // Allocate a per-task spill frame and enqueue the green thread:
            // {fn_index=$w0, arg=$cp (channel base), state=0, frame}. The
            // scheduler dispatches it via call_indirect.
            fprintf(f, "    ;; [WASM Spawn]: allocate frame + enqueue {fn=$w0, arg=$cp, state=0}\n");
            fprintf(f, "    local.get $w0\n    local.get $cp\n    i32.const 0\n    global.get $arena_sp\n    call $teko_enqueue\n");
            fprintf(f, "    global.get $arena_sp\n    i32.const %d\n    i32.add\n    global.set $arena_sp\n", TEKO_WASM_FRAME_BYTES);
            break;

        case OP_AWAIT_INTENT:
            fprintf(f, "    ;; [WASM Await]: cooperative yield to the scheduler\n");
            fprintf(f, "    call $teko_sched_run\n");
            break;

        // ====================================================================
        // 7. FUNCTION BOUNDARIES (green-thread bodies as state machines)
        // ====================================================================
        case OP_FUNC_BEGIN: {
            // Close whatever function is currently open, then open $routine_<id>.
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");          // close $main (result i32)
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "    i32.const 0\n  )\n");            // close previous routine (state 0)
            }
            int n = ctx->wasm_routine_yields;                    // yield points in this routine
            fprintf(f, "  (func $routine_%d (param $arg i32) (param $state i32) (param $frame i32) (result i32)\n", arg);
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32)\n");
            // Import-arg staging slots (Phase 11): a callback routine invoked via
            // $teko_invoke calls dom.* imports just like $main, so it needs $a0..$a2.
            fprintf(f, "    (local $a0 i32) (local $a1 i32) (local $a2 i32)\n");
            // Phase 12 (P12-F): named-local file ($v) for nested-arg spill temps inside
            // a handler body (same module-global count as $main; harmless if unused).
            for (int v = 0; v < ctx->wasm_local_count; v++) {
                fprintf(f, "    (local $v%d i32)\n", v);
            }
            fprintf(f, "    local.get $arg\n    local.set $cp\n");                       // channel base
            fprintf(f, "    local.get $frame\n    i32.load offset=0\n    local.set $w0\n"); // reload spilled $w0
            fprintf(f, "    local.get $frame\n    i32.load offset=4\n    local.set $w1\n"); // reload spilled $w1
            // State dispatch: open (n+1) nested blocks $s_n .. $s0 and br_table on
            // $state. Each yield point (CHAN_GET) closes one block; resuming at
            // state k lands right at yield point k.
            for (int k = n; k >= 0; k--) fprintf(f, "    (block $s%d\n", k);
            fprintf(f, "    local.get $state\n    br_table");
            for (int k = 0; k <= n; k++) fprintf(f, " $s%d", k); // plain (stack-form) instruction, no parens
            fprintf(f, "\n");
            fprintf(f, "    )\n");                                // close (block $s0): start segment follows
            ctx->wasm_open = 2;
            if (ctx->wasm_routine_count < 64) {
                ctx->wasm_routine_ids[ctx->wasm_routine_count] = arg;
            }
            ctx->wasm_routine_count++;
            break;
        }

        case OP_FUNC_END:
            if (ctx->wasm_open == 2) {
                fprintf(f, "    i32.const 0\n  )\n");            // completed: return state 0, close routine
                ctx->wasm_open = 0;
            }
            break;

        // ====================================================================
        // 8. CONTROL FLOW + MODULE CLOSE
        // ====================================================================
        case OP_JMP:
            fprintf(f, "    br $label_%d\n", arg);
            break;

        case OP_JMP_IF_FALSE:
            fprintf(f, "    br_if $label_%d\n", arg);
            break;

        case OP_RETURN:
        case OP_EPILOG: {
            if (ctx->wasm_open == 1) {
                fprintf(f, "    local.get $w0\n  )\n");
            } else if (ctx->wasm_open == 2) {
                fprintf(f, "    i32.const 0\n  )\n");
            }
            ctx->wasm_open = 0;
            int n = ctx->wasm_routine_count;
            fprintf(f, "  (table %d funcref)\n", n > 0 ? n : 1);
            if (n > 0) {
                fprintf(f, "  (elem (i32.const 0)");
                for (int k = 0; k < n && k < 64; k++) {
                    fprintf(f, " $routine_%d", ctx->wasm_routine_ids[k]);
                }
                fprintf(f, ")\n");
            }
            fprintf(f, "  (export \"main\" (func $main))\n");
            // String constant pool → a real (data ...) segment: each pool string laid
            // out NUL-terminated from TEKO_WASM_DATA_BASE (matches OP_SCONST offsets).
            // Falls back to the legacy placeholder when no pool was provided.
            if (ctx->wasm_string_count > 0 && ctx->wasm_strings) {
                fprintf(f, "  (data (i32.const %d) \"", TEKO_WASM_DATA_BASE);
                for (int k = 0; k < ctx->wasm_string_count; k++) {
                    emit_wat_escaped(f, ctx->wasm_strings[k]);
                    fprintf(f, "\\00");
                }
                fprintf(f, "\")\n");
            } else {
                fprintf(f, "  (data (i32.const 1024) \"Hello Teko\\00\")\n");
            }
            fprintf(f, ")\n");
            break;
        }

        default:
            if ((int)op >= 100) {
                fprintf(f, "    ;; Label marker: $label_%d\n", (int)op);
            }
            break;
    }
}
