// Browser-side loader: instantiate each compiled WASM fixture and run its
// in-module test() in-page. The headless-browser harness reads
// `window.__tekoResults` to assert behavioral parity with Node/wasmtime, and
// `window.__crossOriginIsolated` (COOP/COEP) — the prerequisite for Layer B
// (SharedArrayBuffer + Web Workers).
const out = document.getElementById("out");
const fixtures = [
  { file: "../samples/channels.wasm", expected: 42, name: "channels", entry: "test" },
  { file: "../samples/scheduler.wasm", expected: 15, name: "scheduler", entry: "test" },
  // Real compiler output (Phase 10.2b): spawn + blocking channel -> main() == 7.
  { file: "../samples/emitted.wasm", expected: 7, name: "emitted", entry: "main" },
  // Real compiler output (Phase 10.3): mid-function suspension -> main() == 30.
  { file: "../samples/emitted_suspend.wasm", expected: 30, name: "emittedSuspend", entry: "main" },
  // Real compiler output: 5 producers contending on one channel -> main() == 15.
  { file: "../samples/emitted_multi.wasm", expected: 15, name: "emittedMulti", entry: "main" },
];
// Determinism stress: re-instantiate + re-run each module many times; report the
// value only if every run matched.
const REPS = Number(new URLSearchParams(location.search).get("reps") ?? 100);
const results = {};
let lines = [];
for (const { file, expected, name, entry } of fixtures) {
  try {
    const bytes = await (await fetch(file)).arrayBuffer();
    let last = null;
    let allOk = true;
    for (let i = 0; i < REPS; i++) {
      const res = await WebAssembly.instantiate(bytes.slice(0), {});
      last = res.instance.exports[entry]();
      if (last !== expected) { allOk = false; lines.push(`${name}: iter ${i + 1}/${REPS} = ${last} (expected ${expected})`); break; }
    }
    results[name] = allOk ? last : `mismatch(${last})`;
    if (allOk) lines.push(`${name}: ${REPS}/${REPS} runs = ${last}`);
  } catch (e) {
    results[name] = `error: ${e}`;
    lines.push(`${name}: error: ${e}`);
  }
}
window.__tekoResults = results;
window.__crossOriginIsolated = self.crossOriginIsolated === true;
out.textContent = lines.join("\n") + `\ncrossOriginIsolated = ${window.__crossOriginIsolated}`;
