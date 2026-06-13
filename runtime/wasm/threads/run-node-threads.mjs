// Phase 10.4 Layer B harness (node worker_threads): genuine multicore. main()
// runs inside a runner Worker (blocking on memory.atomic.wait32 is only safe off
// the main thread, and keeps the main thread free to bootstrap workers). The
// runner's SPAWN is brokered here on the main thread, which starts a REAL producer
// Worker that re-instantiates the same module against the shared memory and
// publishes the value through the atomic channel.
//
// Modules:
//   samples/threads.wasm          -> 777 (hand-written Layer B reference)
//   samples/emitted_threads.wasm  -> 99  (REAL compiler output, --target wasm-threads)
import { Worker } from "node:worker_threads";
import { readFile } from "node:fs/promises";

const here = (p) => new URL(p, import.meta.url);

async function runModule(wasmRel) {
  const bytes = await readFile(here(wasmRel));
  const memory = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
  const pending = [];
  return await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error("timeout (deadlock?)")), 20000);
    const runner = new Worker(here("./runner.mjs"), { workerData: { memory, bytes } });
    runner.on("error", reject);
    runner.on("message", (m) => {
      if (m && m.spawn) {
        const [fn, arg] = m.spawn;
        const prod = new Worker(here("./worker.mjs"), { workerData: { memory, bytes, fn, arg } });
        pending.push(new Promise((res) => prod.on("message", () => prod.terminate().then(res, res))));
      } else if (m && "result" in m) {
        clearTimeout(timer);
        Promise.all(pending).then(() => runner.terminate()).then(() => resolve(m.result), () => resolve(m.result));
      }
    });
  });
}

const fixtures = [
  { file: "../samples/threads.wasm", expected: 777, name: "threads reference (10.4)" },
  { file: "../samples/emitted_threads.wasm", expected: 99, name: "emitted wasm-threads (10.4)", optional: true },
];

let failures = 0;
for (const { file, expected, name, optional } of fixtures) {
  try {
    const got = await runModule(file);
    if (got === expected) console.log(`OK   ${name}: main() = ${got} (real worker_threads hand-off)`);
    else { console.error(`FAIL ${name}: main() = ${got}, expected ${expected}`); failures++; }
  } catch (e) {
    if (optional && /no such file|ENOENT/.test(String(e))) { console.log(`SKIP ${name}: not built`); continue; }
    console.error(`FAIL ${name}: ${e}`); failures++;
  }
}
process.exit(failures === 0 ? 0 : 1);
