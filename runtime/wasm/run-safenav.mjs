// Phase 18.B — SAFE NAVIGATION `?.` over optional objects on the WASM target. safenav.tks is
// compiled by the teko BINARY to a WASM module whose OP_OBJ_*/OP_CALL_FUNC member access is
// reactor-backed (teko_rt_object_* in crypto.wasm over the shared memory — the SAME C object store
// as native). An optional object carries the 18.A present companion + its `?Box`-annotated class;
// `obj?.field`/`obj?.method(args)` is GUARDED by WASM `(if … end)` on the present flag (the same
// OP_IF lowering as native je/cbz): present → the access runs; null → it is SKIPPED (no deref of a
// null handle) and the empty-optional result lets a trailing Elvis `?? d` default. No new IL/runtime.
// This harness instantiates the reactor + the sample against one shared linear memory, runs `main`,
// and asserts the output is BYTE-FOR-BYTE identical to the native proof (samples/safenav.tks).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout.
const expected = ["a = 21", "b = 42", "c = -1", "d = -1"];

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor links the whole runtime; stub the host facts it imports so it instantiates.
  teko_now_ns: () => process.hrtime.bigint(),
  teko_now_unix: () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = (i * 7 + 1) & 0xff;
  },
  emit: (ptr) => {
    const u = new Uint8Array(memory.buffer);
    let e = ptr >>> 0;
    while (u[e] !== 0) e++;
    out.push(dec.decode(u.subarray(ptr >>> 0, e)));
  },
};

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
const reactor = await WebAssembly.instantiate(reactorBytes, { env });
const sample = await readFile(here("./samples/safenav.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL safenav:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   safenav: ${out.length} lines matched native byte-for-byte (?. null-propagation over optional objects)`);
process.exit(0);
