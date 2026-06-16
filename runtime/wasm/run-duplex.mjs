// Phase 14 (14.B) — duplex channel on the WASM target. The teko binary compiles
// samples/duplex.tks to a module whose OP_DUPLEX_* ops import teko_rt_duplex_* from the
// compiled-C runtime reactor (crypto.wasm) and share ONE linear memory with it — the SAME
// duplex C runtime as the native runner (runtime/native/samples/duplex.tks), not a second
// implementation. This host instantiates the reactor + the sample against shared memory,
// captures env.log_int, and asserts the bidirectional values + the structured CLOSED status.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor imports env.teko_random (crypto); duplex doesn't use it — stub it.
  teko_now_ns: () => process.hrtime.bigint(), // reactor delayed/retry clock (real ns)
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/duplex.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [111, 222, 3]; // 0->1, 1->0, then poll-after-close = TEKO_DX_CLOSED
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   duplex(wasm): bidirectional ${JSON.stringify(out.slice(0,2))} + structured CLOSED status ${out[2]} (reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL duplex(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
