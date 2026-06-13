# Teko WASM test harness (Phase 10)

Executable harness for the WASM concurrency backend. It runs the same module in
three places to guarantee behavioral parity:

- **Node** (`run-node.mjs`) — standalone-engine smoke (also covers wasmtime-class engines).
- **wasmtime** — standalone CLI, in CI.
- **Headless browser** (`run-browser.mjs`, Playwright + Chromium) — required because
  Teko WASM may ship as an add-in to a browser host even when there is no browser
  *target*. The browser run also verifies `crossOriginIsolated` (COOP/COEP), the
  prerequisite for Layer B (`--target=wasm-threads`, SharedArrayBuffer + Web Workers).

The current fixture is `samples/channels.wat`, a hand-written mirror of the
Phase 10.1 channel ring buffer emitted by `emit_wasm.c`. As later increments wire
the full `.tks → .wasm` pipeline, the fixture is replaced by compiler output.

## Build the fixture

```sh
# needs WABT (wat2wasm). macOS: brew install wabt ; Ubuntu: apt-get install wabt
wat2wasm samples/channels.wat -o samples/channels.wasm
```

## Run locally

```sh
node run-node.mjs                 # standalone engine; expects "OK: channel round-trip = 42"
# or under wasmtime:  wasmtime run --invoke test samples/channels.wasm   # => 42

npm install                       # one-time, for the browser harness
npx playwright install chromium   # one-time
node run-browser.mjs              # serves with COOP/COEP, drives headless Chromium

node server.mjs                   # manual: serve at http://localhost:8092 (COOP/COEP on)
```

## CI

Two **non-blocking** (`continue-on-error`) jobs in `.github/workflows/ci.yml`:
`wasm-wasmtime` (standalone exec) and `wasm-browser` (headless Chromium). They are
non-blocking until the WASM concurrency backend stabilizes, then promoted to gating.
