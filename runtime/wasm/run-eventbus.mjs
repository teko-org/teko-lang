// Phase 15 (15.D) — event subsystem (`event`/`subscribe`/`raise`) on the WASM target. The teko
// binary compiles samples/eventbus.tks; `raise Ping(5)` fan-outs to the two subscribers (fanout +
// fire_and_forget), each spawned over the in-module Phase-14 cooperative scheduler and drained at
// $main close. So the main body (1, 2) runs first, then the handlers (15, 25) — deferred fan-out
// with the raised argument. Asserts [1, 2, 15, 25], matching the native runner.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 16 });
const out = [];
const env = {
  memory,
  log_int: (n) => { out.push(n | 0); },
};

// The event sample uses only the in-module scheduler (no reactor) — instantiate it directly.
const sample = await readFile(here("./samples/eventbus.wasm"));
const teko = await WebAssembly.instantiate(sample, { env });
teko.instance.exports.main();

const expected = [1, 2, 15, 25];
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   eventbus(wasm): raise fan-out to 2 subscribers (deferred) -> ${JSON.stringify(out)} (1,2 main; 15,25 handlers at exit)`);
  process.exit(0);
} else {
  console.error(`FAIL eventbus(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
