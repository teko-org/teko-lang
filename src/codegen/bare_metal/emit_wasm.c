#include "../codegen_metal.h"
#include <stdio.h>
#include <string.h>

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
            fprintf(f, "  (memory 1)\n");
            fprintf(f, "  (export \"memory\" (memory 0))\n");
            fprintf(f, "  (global $arena_sp (mut i32) (i32.const 2048))\n");
            emit_wasm_scheduler_runtime(f);
            emit_wasm_heap_runtime(f);         // Phase 11 MVP-4: real freeing allocator
            fprintf(f, "  (func $main (result i32)\n");
            // $w0 accumulator, $w1 scratch, $cp channel ptr, $a0..$a2 import-arg
            // staging slots (Phase 11 multi-param imports — see OP_SETARG).
            fprintf(f, "    (local $w0 i32) (local $w1 i32) (local $cp i32)\n");
            fprintf(f, "    (local $a0 i32) (local $a1 i32) (local $a2 i32)\n");
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
