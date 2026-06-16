// Phase 15 (15.C) — generics / per-type monomorphization on the WASM target. The teko binary
// compiles samples/generics.tks, monomorphizing `Factory<T>` into Factory$Circle / Factory$Square
// at compile time (T() instantiates the concrete type, t.tag() statically dispatches). Objects use
// the reactor-backed teko_object store (shared linear memory); methods dispatch via the in-module
// call_indirect table. Asserts [11, 22] — same as the native runner.
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
const sample = await readFile(here("./samples/generics.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [11, 22];
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   generics(wasm): per-type monomorphization -> ${JSON.stringify(out)} (Factory<Circle>/Factory<Square> specialized at compile time)`);
  process.exit(0);
} else {
  console.error(`FAIL generics(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
