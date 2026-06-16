// Phase 15 (15.A) — concrete class on the WASM target. The teko binary compiles
// samples/class.tks to a module whose OP_OBJ_* ops import teko_rt_object_* from the compiled-C
// runtime reactor (crypto.wasm) and share ONE linear memory with it — the SAME teko_object C
// runtime as the native runner (runtime/native/samples/class.tks), not a second implementation.
// Method dispatch (OP_CALL_FUNC) uses the in-module cooperative table (call_indirect) with `self`
// passed as the first argument. This host instantiates the reactor + the sample against shared
// memory, captures env.log_int, and asserts p.sum()=7 and p.scale(10)=70.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor imports these host hooks (crypto/time); the class sample uses none — stub them.
  teko_now_ns: () => process.hrtime.bigint(),
  teko_now_unix: () => 1000000000n, teko_tz_offset: () => 0,
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/class.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [7, 70]; // p.sum() = 3+4, p.scale(10) = (3+4)*10
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   class(wasm): fields + static method dispatch -> ${JSON.stringify(out)} (reactor-backed objects, call_indirect methods)`);
  process.exit(0);
} else {
  console.error(`FAIL class(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
