// Phase 14 (14.D) — broadcast (non-destructive 1:N pub-sub) on the WASM target. The teko binary
// compiles samples/broadcast.tks to a module whose OP_BCAST_* ops import teko_rt_bcast_* from the
// compiled-C reactor (crypto.wasm) and share ONE linear memory with it — the SAME broadcast C
// runtime as the native runner. This host instantiates the reactor + the sample against shared
// memory, captures env.log_int, and asserts both subscribers independently see every value.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_now_ns: () => process.hrtime.bigint(), // reactor delayed/retry clock (real ns)
  teko_random: (ptr, len) => { // reactor (crypto) import; unused by broadcast — stub
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/broadcast.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [10, 20, 10, 20]; // subscriber 0 sees both, subscriber 1 independently sees both
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   broadcast(wasm): 2 subscribers each saw every value ${JSON.stringify(out)} (reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL broadcast(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
