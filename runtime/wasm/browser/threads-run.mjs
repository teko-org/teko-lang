// Layer B browser driver (page side). For each shared-memory module it creates a
// shared WebAssembly.Memory, runs main() in a runner Worker, and brokers the
// runner's SPAWN request by starting a producer Worker against the same memory —
// genuine multicore in the browser. Results land on window.__tekoThreads for the
// Playwright harness; window.__crossOriginIsolated confirms COOP/COEP.
const out = document.getElementById("out");
const fixtures = [
  { file: "../samples/threads.wasm", expected: 777, name: "threadsRef" },
  { file: "../samples/emitted_threads.wasm", expected: 99, name: "threadsEmitted" },
];

function runModule(bytes) {
  return new Promise((resolve) => {
    const memory = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
    const runner = new Worker(new URL("./threads-runner.mjs", import.meta.url), { type: "module" });
    const producers = [];
    const finish = (v) => { clearTimeout(timer); runner.terminate(); producers.forEach((p) => p.terminate()); resolve(v); };
    const timer = setTimeout(() => finish("timeout"), 15000);
    runner.onmessage = (e) => {
      if (e.data.spawn) {
        const [fn, arg] = e.data.spawn;
        const prod = new Worker(new URL("./threads-producer.mjs", import.meta.url), { type: "module" });
        producers.push(prod);
        prod.postMessage({ memory, bytes, fn, arg });
      } else if ("result" in e.data) {
        finish(e.data.result);
      }
    };
    runner.postMessage({ memory, bytes });
  });
}

// STRESS: repeat the real Web Worker hand-off many times; report the value only
// if EVERY iteration matched, else an error string (so the Playwright harness fails).
const REPS = Number(new URLSearchParams(location.search).get("reps") ?? 30);
const results = {};
const lines = [];
for (const { file, expected, name } of fixtures) {
  try {
    const bytes = await (await fetch(file)).arrayBuffer();
    let last = null;
    let allOk = true;
    for (let i = 0; i < REPS; i++) {
      last = await runModule(bytes.slice(0));
      if (last !== expected) { allOk = false; lines.push(`${name}: iter ${i + 1}/${REPS} = ${last} (expected ${expected})`); break; }
    }
    results[name] = allOk ? last : `mismatch(${last})`;
    if (allOk) lines.push(`${name}: ${REPS}/${REPS} hand-offs = ${last}`);
  } catch (e) {
    results[name] = `error: ${e}`;
    lines.push(`${name}: error: ${e}`);
  }
}
window.__tekoThreads = results;
window.__crossOriginIsolated = self.crossOriginIsolated === true;
out.textContent = lines.join("\n") + `\ncrossOriginIsolated = ${window.__crossOriginIsolated}`;
