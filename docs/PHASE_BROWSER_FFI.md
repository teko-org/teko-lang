# Phase — Browser FFI / JS-DOM Interop (design & plan)

> Branch: `feat/browser-ffi-interop`. Goal: when targeting browser WASM, give Teko an
> ergonomic, low-friction two-way bridge to JS/DOM. This document is the design + the
> incremental plan; MVP-1 (foundation) is implemented first.

## Roadmap numbering (confirmed — Phase 11; `docs/plan.md` renumbered)

This is a dedicated **Phase 11 — Browser FFI / JS-DOM Interop**; the memorandum roadmap
was pushed **+1** in `docs/plan.md` (former Phases 11–19 → 12–20):

| Was | Now |
|-----|-----|
| Phase 10 — WASM Concurrency Backend | Phase 10 (unchanged, merged) |
| — | **Phase 11 — Browser FFI / JS-DOM Interop (NEW)** |
| Phase 11 — Frontend Grammar & Lexer Extension | Phase 12 |
| Phase 12 — Advanced Concurrency, Signaling & Duplex Channels | Phase 13 |
| Phase 13 — Bare-Metal OOP | Phase 14 |
| Phase 14 — Optionals & Compile-Time Metaprogramming | Phase 15 |
| Phase 15 — Native Networking, Web & Cryptography | Phase 16 |
| Phase 16 — Enterprise Parsers & Template Compiler | Phase 17 |
| Phase 17 — Interoperability & Rich Metadata (`.teko_meta`) | Phase 18 |
| Phase 18 — Native Testing (`.tkt`) & Coverage | Phase 19 |
| Phase 19 — Self-Containment (Self-Hosting) | Phase 20 |

`docs/plan.md` (intro, ASCII diagram, headers, Documentation Map, cross-refs) is renumbered
accordingly; Self-Containment is now Phase 20.

## Current state (verified in code, June 2026)

- `extern fn … from "ns" as "name"` is **parsed** (`src/parser_ffi.c`) into a full
  `FFIFunctionNode` (fn_name, return_type, alias, params[]) but **discarded** after
  visibility wrapping (`src/parser_visibility.c:99`) — nothing lowers it.
- The IL has a deduplicated `ConstantStringPool` in `BytecodeBuffer`
  (`src/codegen_li.h`), but it is **siloed**: the metal emitter
  (`teko_metal_emit_program(ctx, bytecode, size)`) only receives a raw byte array, so
  `OP_SCONST` emits a placeholder `i32.const (idx*32)` and the only `(data …)` is a
  hardcoded `"Hello Teko"`.
- There is **no call opcode** and **no real `.tks → .wat` driver** (`main.c` emits from
  a mock bytecode array). Executable proof therefore drives the emitter directly via an
  `emit-demo` (the pattern established in Phase 10).

## Architecture (target)

- **Teko → JS/DOM (imports):** `extern fn print(x: i32) from "env" as "log"` lowers to a
  WASM `(import "env" "log" (func $import_0 (param i32)))` and a new `OP_CALL_IMPORT`.
  `@dom`/`@js` intrinsics become imports in a `dom`/`js` namespace, wired by an
  auto-generated JS glue that marshals strings (a `(ptr,len)` pair over linear memory),
  DOM-node handles (an integer handle table on the JS side), and events (a Teko
  function-table index registered as a listener).
- **JS → Teko (exports):** named `pub fn` exports + a JS facade.
- **Strings:** the constant pool is emitted as a real `(data …)` segment; `OP_SCONST`
  yields the actual byte offset. JS reads `(ptr,len)` via `TextDecoder`.

## Incremental plan

- **MVP-1a — string pool. ✅ delivered.** *(commit `6fed9c6`)*
  - String pool → real `(data …)` segment; `OP_SCONST` → correct offset (the pool is
    threaded through `MetalContext`). JS reads `(ptr,len)` via `TextDecoder`.
- **MVP-1b — host imports. ✅ delivered.** *(commit `54a3a37`)*
  - `OP_CALL_IMPORT` (`0x09`) + an import table in `MetalContext`; emit `(import "ns"
    "name" (func …))` in the prologue (before any definition) and `call $import_N` at the
    call site.
  - Executable proof: `emit-demo/emit_ffi.c` imports a host `env.log` and calls it;
    `run-ffi.mjs` reads the pooled string back from memory (CI: "hello from teko").
    Goldens pin the `(import …)` and `(data …)`.
  - Native emitters (the 16) did not regress; the new opcode is below the native label
    threshold and ignored by them.
- **MVP-2 — DOM. ✅ delivered.**
  - Multi-param imports: `OP_SETARG` (`0x0A`) stages args into `$a0..$a2`; `OP_CALL_IMPORT`
    pushes `$a0..$a(n-2)` then `$w0` (last arg = accumulator). Single-arg (MVP-1) unchanged.
  - `dom` import namespace + an **auto-generated** `<mod>.glue.mjs`
    (`teko_metal_emit_dom_glue`): `(ptr,len)` string marshalling via `TextDecoder` over
    linear memory + an `i32 → Element` handle table. **No dev boilerplate.**
  - **Minimal, honest DOM vocabulary** (everything MVP-2 covers — nothing more):
    `createElement(tag) → handle`, `getElementById(id) → handle`, `setText(handle, text)`,
    `appendChild(parent, child)`. The glue only emits methods the module actually imports.
  - Direction: **Teko → JS only** (strings read out of memory). Writing JS strings *back*
    into memory needs a real allocator — deliberately deferred (see MVP-4 / Decisions).
  - Executable proof: `emit-demo/emit_dom.c` builds a `<span>"hello from teko"` and appends
    it to `#out`; `run-dom.mjs` (Playwright, COOP/COEP) asserts `#out > span` textContent.
    Golden `test_teko_aot_wasm_dom_import_lowering` pins the imports, the `$a*` staging, the
    call shape, and the generated glue.
- **MVP-3 — events/callbacks. ✅ delivered.**
  - JS→Teko: `OP_SETARG` already lets a callback routine call `dom.*`; the module now
    exports a `teko_invoke(fn_index, arg)` dispatcher that `call_indirect`s a table slot
    with the same routine ABI the cooperative scheduler uses (frame[0]=arg → callee `$w0`).
  - New `dom.on(handle, "event", fn_index)` import; the glue registers
    `handles[h].addEventListener(event, () => teko_invoke(fn, h))`, passing the attached
    handle as the callback arg. Callback routines get the `$a*` staging locals.
  - **Surface:** one-way registration `dom.on` + synchronous callback dispatch. The
    callback receives the **attached element's handle** (an `i32`) — no event object is
    marshalled (that needs richer marshalling / an allocator; deferred).
  - Fixed a latent CSE bug surfaced here: ops that clobber `$w0` (`SCONST`/`LOAD`/
    `CHAN_GET`/`CALL_IMPORT`) now invalidate the ICONST reuse cache. Layer A fixtures
    still 42/15/7/30/15 (500×).
  - Executable proof: `emit-demo/emit_events.c` registers a click listener whose Teko
    handler sets the text; `run-events.mjs` (Playwright) clicks `#count` and asserts
    `"0" → "clicked!"`. Golden `test_teko_aot_wasm_event_callback_lowering`.
- **MVP-4 — real allocator + ergonomic facades. ✅ delivered (closes Phase 11).**
  - **Real allocator** (`emit_wasm_heap_runtime`): an implicit free-list over a heap
    region `[16384..65536)` **dedicated and separate from the cooperative bump arena**
    (the two regions never mix) — 8-byte-aligned blocks with an 8-byte header
    `{size, free}`, `teko_alloc` first-fit + split, `teko_free` **validates** the pointer
    (real, currently-used block start) then marks + **forward-coalesces** adjacent free
    blocks, `teko_reset` bulk reclaim. Exported `teko_alloc`/`teko_free`/`teko_reset`;
    lazily initialized; OOM→0; one page.
    *Strategy (per the directive — simplicity + correctness over performance): free-list
    first-fit with block headers + coalescing of adjacent free blocks, 8-byte aligned, in
    a dedicated heap. Size-class segregation is a possible future optimization; a
    growable/unified arena+heap and a `(memory)` grow path are deferred.*
    *Non-negotiable criteria met: real reuse of freed memory; `teko_free` of
    null/out-of-range/interior/double-freed pointers is a safe no-op; a dedicated 10k-cycle
    alloc→free→alloc stress proves memory is reused (the high-water mark stays near the
    heap base, not growing) with no leak/overflow/double-free; ASan/UBSan clean on both
    dispatch paths + TSan; deterministic and testable from Node/browser + a C golden.*
  - **JS→Teko strings:** glue/facade `TextEncoder` → `teko_alloc` → copy → `(ptr,len)`.
    `teko_invoke2(fn, a0, a1)` delivers both to the callee (`$w0`,`$w1`).
  - **Ergonomic facade** (`teko_metal_emit_facade` → `<mod>.mjs`): `async instantiate(bytes)`
    wires glue+module and returns `mod.<name>(str)` that marshals the JS string and
    dispatches the Teko routine. No dev boilerplate. Proof: `emit_alloc.c` +
    `run-facade.mjs` — `mod.showMessage("hello from JS via alloc")` → `#out`.
  - **Rich event payload** (`dom.on_value`): the listener marshals the event target's
    `.value` via the allocator and calls back with `(ptr,len)`. Proof: `emit_richevent.c`
    + `run-richevent.mjs` — typing into `#inp` mirrors into `#echo`.
  - Allocator stress: `run-alloc.mjs` (range/overlap/reuse/coalesce/double-free/reset/OOM).
    Goldens: `…heap_allocator_runtime`, `…facade_and_rich_event_lowering`. Suite 87/87;
    ASan+UBSan (both paths) + TSan clean; Layer A fixtures 42/15/7/30/15 (500×).

**Phase 11 is complete** — MVP-1a/1b/2/3/4 all ✅, CI-green on `feat/browser-ffi-interop`
(PR #4). The Browser FFI backend is fully exercised via the emit-demos; the only remaining
Browser-FFI-adjacent work (real `.tks` frontend lowering, below) is independent and
explicitly out of this phase's scope.

- **FE — real `.tks` → IL → WASM frontend. ✅ delivered (no more mock bytecode).**
  - **FE-A/B:** `codegen_li` gains an import table + interop IL emit helpers, and
    `codegen_li_emit_wasm` bridges an IL `BytecodeBuffer` to the WASM backend (string pool
    → `(data)`, import table → `(import …)`, IL → module, + auto glue/facade).
  - **FE-C:** `frontend_interop.c` (`teko_compile_interop`) compiles the interop subset of
    real Teko source, driving the real lexer and reusing the real `parse_extern_declaration`
    — `extern fn … from "ns" as "name"` is now **consumed**, not discarded, and a call to
    one lowers to `OP_SCONST/OP_ICONST` + `OP_SETARG` + `OP_CALL_IMPORT`.
  - **FE-D:** `main.c --target=wasm` reads a real `.tks` and emits a real `.wat` (+glue) —
    **mock bytecode removed on the WASM path**. Proof: the `teko` binary compiles
    `samples/hello.tks` and `run-source.mjs` runs it.
  - **FE-E:** `@dom.method(...)` / `@js.method(...)` intrinsics from source → `dom.*`/`js.*`
    imports; strings expand to `(ptr,len)`; the leading arg may be a nested `@dom` call.
    Proof: `samples/dom.tks` compiled by the binary drives the DOM in headless Chromium.
  - **FE-F:** event handlers from source — a `fn` handler is lowered to a table routine; the
    handler param (the event arg) is stashed/loaded; `@dom.on(elem, "click", handler)`
    resolves the handler to its table slot. Proof: `samples/events.tks` — a click fires the
    Teko `fn` via `teko_invoke` and updates the DOM in Chromium.
  - Bounded subset (documented): args are string/int literals, a handler param ref, a
    function ref, or one leading nested `@dom` call; named locals / general expressions /
    multiple nested handle args are future work. The interop surface (extern/@dom/strings/
    events) compiles from source end-to-end.

Discipline: 1 increment per commit; Release + ASan/UBSan on both dispatch paths (TSan for
the allocator); native emitter goldens unchanged; the 4 CI workflows green; patient
watcher; no merge/force-push.
