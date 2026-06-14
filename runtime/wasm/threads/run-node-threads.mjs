// Phase 10.4 Layer B harness (node worker_threads): genuine multicore. main()
// runs inside a runner Worker (it busy-polls the channel flag; doing that on the
// main thread would peg it and starve worker bootstrap). The runner's SPAWN is
// brokered here on the main thread, which starts a REAL producer Worker that
// re-instantiates the same module against the shared memory and publishes the
// value through the atomic channel.
//
// Layer B deliberately does NOT use memory.atomic.wait32/notify: that primitive
// was observed to never make progress across instances on the GitHub runner. The
// receive busy-polls with i32.atomic.load instead — shared-memory visibility is
// guaranteed, so no wakeup mechanism is needed for correctness.
//
// Hardened so it can never hang: a watchdog tears down every worker and fails with
// a clear message; runner/producer errors are surfaced rather than waited on.
//
// Modules:
//   samples/threads.wasm          -> 777 (hand-written Layer B reference)
//   samples/emitted_threads.wasm  -> 99  (REAL compiler output, --target wasm-threads)
import { Worker } from "node:worker_threads";
import { readFile } from "node:fs/promises";

const here = (p) => new URL(p, import.meta.url);
const WATCHDOG_MS = Number(process.env.TEKO_THREADS_TIMEOUT_MS ?? 30000);

async function runModule(wasmRel) {
  const bytes = await readFile(here(wasmRel));
  const memory = new WebAssembly.Memory({ initial: 1, maximum: 1, shared: true });
  const workers = [];
  const cleanup = () => workers.forEach((w) => w.terminate().catch(() => {}));
  return await new Promise((resolve, reject) => {
    const fail = (err) => { clearTimeout(timer); cleanup(); reject(err instanceof Error ? err : new Error(String(err))); };
    const timer = setTimeout(() => fail(new Error(`watchdog: no progress within ${WATCHDOG_MS}ms`)), WATCHDOG_MS);
    const pending = [];
    const runner = new Worker(here("./runner.mjs"), { workerData: { memory, bytes } });
    workers.push(runner);
    runner.on("error", fail);
    runner.on("message", (m) => {
      if (m && m.spawn) {
        const [fn, arg] = m.spawn;
        const prod = new Worker(here("./worker.mjs"), { workerData: { memory, bytes, fn, arg } });
        workers.push(prod);
        prod.on("error", fail);
        pending.push(new Promise((res) => prod.on("message", (pm) => {
          if (pm && pm.error) fail(new Error(`producer: ${pm.error}`));
          res();
        })));
      } else if (m && m.error) {
        fail(new Error(`runner: ${m.error}`));
      } else if (m && "result" in m) {
        clearTimeout(timer);
        Promise.allSettled(pending).then(() => { cleanup(); resolve(m.result); });
      }
    });
  });
}

const fixtures = [
  { file: "../samples/threads.wasm", expected: 777, name: "threads reference (10.4)" },
  { file: "../samples/emitted_threads.wasm", expected: 99, name: "emitted wasm-threads (10.4)", optional: true },
];

// STRESS: repeat the real cross-thread hand-off many times per run so scheduling
// flakiness a single iteration would hide gets surfaced. Any wrong value or
// timeout in any iteration fails the job.
const REPS = Number(process.env.TEKO_THREADS_REPS ?? 100);

let failures = 0;
for (const { file, expected, name, optional } of fixtures) {
  try {
    let ok = 0;
    for (let i = 0; i < REPS; i++) {
      const got = await runModule(file);
      if (got !== expected) { console.error(`FAIL ${name} iter ${i + 1}/${REPS}: main() = ${got}, expected ${expected}`); failures++; break; }
      ok++;
    }
    if (ok === REPS) console.log(`OK   ${name}: ${REPS}/${REPS} real worker_threads hand-offs all = ${expected}`);
  } catch (e) {
    if (optional && /no such file|ENOENT/.test(String(e))) { console.log(`SKIP ${name}: not built`); continue; }
    console.error(`FAIL ${name}: ${e}`); failures++;
  }
}
process.exit(failures === 0 ? 0 : 1);
