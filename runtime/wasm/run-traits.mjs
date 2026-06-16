// Phase 15 (15.B) — abstract/trait dynamic dispatch on the WASM target. The teko binary compiles
// samples/traits.tks to a module whose OP_VTABLE_* + OP_OBJ_* ops import teko_rt_vtable_* /
// teko_rt_object_* from the compiled-C runtime reactor (crypto.wasm) and share ONE linear memory
// with it — the SAME teko_vtable / teko_object C runtimes as the native runner, not a second
// implementation. Method dispatch (OP_CALL_FUNC) uses the in-module call_indirect table. A
// Shape-typed fat reference dispatches area()/to_string() to Circle then (after reassignment)
// Square by the runtime type_id. Asserts [12, 112, 9, 209] — `to_string` rides the same vtable.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_now_ns: () => process.hrtime.bigint(),
  teko_now_unix: () => 1000000000n, teko_tz_offset: () => 0,
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/traits.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [12, 112, 9, 209];
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   traits(wasm): trait dynamic dispatch via static vtable -> ${JSON.stringify(out)} (Circle/Square area + to_string, reactor-backed vtable)`);
  process.exit(0);
} else {
  console.error(`FAIL traits(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
