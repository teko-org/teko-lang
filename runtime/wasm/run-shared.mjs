// Phase 14 (14.E) — automated shared memory (`shared { }` + `atomic.*`) on WASM. The teko binary
// compiles samples/shared.tks to a module whose OP_SHARED_*/OP_ATOMIC_* import teko_shared_*/
// teko_atomic_* from the compiled-C reactor (crypto.wasm) and share ONE linear memory with it —
// the SAME shared-memory C runtime as the native runner. This host instantiates the reactor + the
// sample against shared memory, captures env.log_int, and asserts the atomic accumulation inside
// the coarse-locked block.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_now_ns: () => process.hrtime.bigint(), // reactor delayed/retry clock (real ns)
  teko_random: (ptr, len) => { // reactor (crypto) import; unused by shared — stub
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/shared.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [8, 10]; // 5+3 inside the shared block, then +2
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   shared(wasm): atomic accumulation in a coarse-locked block ${JSON.stringify(out)} (reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL shared(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
