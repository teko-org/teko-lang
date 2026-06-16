// Phase 14 (14.F) — resilience (`retry`/`circuit`) on the WASM target. The teko binary compiles
// samples/resilience.tks to a module whose OP_RETRY_*/OP_CIRCUIT_* import teko_rt_retry_* /
// teko_rt_circuit_* from the compiled-C reactor (crypto.wasm) and share ONE linear memory with it
// — the SAME teko_retry C policy as the native runner. This host instantiates the reactor + the
// sample against shared memory, captures env.log_int, and asserts: retry succeeds on the 3rd
// attempt (tries=3); retry exhaustion runs the fallback (777) with t2=2; the circuit breaker trips
// after 2 failures so only 2 calls run the body (ran=2) and every iteration falls back (opened=5).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_now_ns: () => process.hrtime.bigint(), // reactor delayed/retry clock (real ns)
  teko_random: (ptr, len) => { const u = new Uint8Array(memory.buffer); for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0; },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/resilience.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [3, 777, 2, 555, 1, 444, 3, 2, 5]; // succeed-on-3rd; exhaust(777,t2=2); timeout(555,tt=1); log(444,lg=3); circuit(ran=2,opened=5)
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   resilience(wasm): retry succeed-on-3rd + exhaust->fallback + circuit trip ${JSON.stringify(out)} (reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL resilience(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
