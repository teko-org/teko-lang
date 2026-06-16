// Phase 16 (16.A) — culture-invariant conversion surface on the WASM target.
//
// convert.tks is compiled by the teko BINARY to a WASM module that lowers the conversion surface
// (OP_CALL_RUNTIME ids 49/51/52) to IMPORTED entry points in the compiled-C reactor (crypto.wasm).
// The reactor is the SAME teko_convert.c source of truth as native (src/runtime/teko_convert.c),
// compiled to wasm32; both modules share ONE host-owned linear memory. This harness instantiates
// the reactor + the sample against that shared memory and asserts the emitted output is byte-for-
// byte identical to the native proof (runtime/native/run-native.sh) — proving WASM lowers to the
// same implementation, locale-invariant (`.`-decimal, no digit grouping, canonical true/false).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

const expected = ["42", "1000000", "true", "false", "x = 42"];

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor links the whole runtime (retry/delayed/time/crypto), so it imports these host
  // facts even though the conversion surface uses none of them — stub them so it instantiates.
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
const sample = await readFile(here("./samples/convert.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL convert:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   convert: ${out.length} conversions matched (reactor-backed, locale-invariant)`);
process.exit(0);
