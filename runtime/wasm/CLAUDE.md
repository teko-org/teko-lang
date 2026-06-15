# CLAUDE.md — `runtime/wasm/` (WASM harness)

See the root `CLAUDE.md` for the backend design and CI. This is the host-side test harness.

## Two proof paths: real `.tks` source, and emit-demos
The CLI now compiles **real Teko source** for the WASM target:
`teko build <file>.tks --target=wasm -o out.wat` lexes/parses/lowers the interop subset
(`extern`, `@dom`/`@js`, strings, `fn` event handlers) to IL → `.wat` (+ auto glue/facade),
no mock bytecode (`frontend_interop.c` / `codegen_li_wasm.c`). The source proofs:
`samples/hello.tks` (`run-source.mjs`), `samples/dom.tks` (`run-dom-source.mjs`),
`samples/events.tks` (`run-events-source.mjs`).
The **emit-demos** remain as direct-backend proofs (and cover backend features whose source
syntax is still future work): each `emit-demo/*.c` links `build/libteko_core.a`, builds an IL
byte program, and calls `teko_metal_emit_program` to write a `.wat`. `wat2wasm` assembles;
the runners execute and assert. (Locally, `npm i wabt` gives a JS `wat2wasm`.)

## Layout
- `emit-demo/` — C drivers: `emit_spawn_channel.c` (→7), `emit_suspend.c` (→30),
  `emit_multi.c` (→15), `emit_threads.c` (Layer B →99), `emit_ffi.c` (Browser FFI MVP-1:
  `(import "env" "log")` + `OP_CALL_IMPORT` + pooled string), `emit_dom.c` (MVP-2: multi-arg
  `dom.*` imports + `OP_SETARG` + auto-generated `.glue.mjs`), `emit_events.c` (MVP-3:
  `dom.on` + exported `teko_invoke` callback dispatcher), `emit_alloc.c` (MVP-4: facade
  `<mod>.mjs` + JS→Teko strings via `teko_alloc`), `emit_richevent.c` (MVP-4: `dom.on_value`
  rich event payload). Output `.wat`/`.glue.mjs`/`.mjs` gitignored.
- `samples/` — hand-written reference fixtures: `channels.wat` (42), `scheduler.wat` (15),
  `threads.wat` (Layer B reference, 777). `emitted*.wat`/`*.wasm` are generated/gitignored.
- `run-node.mjs`, `run-browser.mjs` — Layer A under Node / headless Chromium.
- `run-ffi.mjs` — Browser FFI MVP-1 proof: supplies host `env.log(ptr)`, reads the
  NUL-terminated pooled string back from exported memory, asserts "hello from teko".
- `run-dom.mjs` + `browser/dom.html` + `browser/dom-run.mjs` — Browser FFI MVP-2 proof
  (Playwright, COOP/COEP): Teko builds a `<span>"hello from teko"` and appends it to `#out`
  entirely through the auto-generated `dom.*` glue; the harness asserts `#out > span` text.
  Strings cross as `(ptr,len)`; DOM nodes as `i32` handles (Teko→JS read direction only).
- `run-events.mjs` + `browser/events.html` + `browser/events-run.mjs` — Browser FFI MVP-3
  proof (JS→Teko): `$main` registers a click listener via `dom.on`; the harness clicks
  `#count` and the glue calls `exports.teko_invoke(fn, handle)`, dispatching the Teko
  callback that sets the text (`"0" → "clicked!"`). The glue is now
  `makeTekoDomImports(getMemory, getInstance)` — the 2nd thunk exposes `teko_invoke`.
- `run-alloc.mjs` — MVP-4 allocator stress on `emitted.wasm`'s exported
  `teko_alloc`/`teko_free`/`teko_reset` (range/overlap/reuse/coalesce/double-free/reset/OOM
  + a 10k-cycle alloc→free→alloc loop asserting bounded reuse, and null/wild/interior frees
  as safe no-ops). `teko_free` validates the pointer before freeing.
- `run-facade.mjs` + `browser/facade.html` + `browser/facade-run.mjs` — MVP-4 facade +
  JS→Teko string: the auto-generated `emitted_alloc.mjs` exposes `mod.showMessage(str)`,
  which `teko_alloc`s the JS string, copies bytes, and dispatches the Teko routine → `#out`.
- `run-richevent.mjs` + `browser/richevent.html` + `browser/richevent-run.mjs` — MVP-4 rich
  event: `dom.on_value` marshals the input's value via the allocator → `teko_invoke2(fn,
  ptr,len)` → Teko mirrors it to `#echo`. Exports: `teko_alloc/teko_free/teko_reset/
  teko_invoke2` are emitted in every Layer A module.
- `threads/` (`run-node-threads.mjs`, `runner.mjs`, `worker.mjs`) — Layer B via
  `worker_threads`. `browser/threads-*.mjs` + `run-threads-browser.mjs` — Layer B via Web Workers.
- `server.mjs` — static server with COOP/COEP (required for SharedArrayBuffer / Layer B).

## Layer B note
`main()` blocks, so it runs in a **runner worker** (the node main thread must stay free to
bootstrap the producer). The channel **receive busy-polls** the flag (bounded, `unreachable`
cap) — cross-instance `memory.atomic.notify` is unreliable on the GitHub runner; plain shared
atomic loads are not. Harnesses always carry a watchdog so a hang fails fast.
