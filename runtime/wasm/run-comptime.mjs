// Phase 18.D — `comptime` compile-time evaluation on the WASM target. comptime.tks is compiled by the
// teko BINARY to a WASM module. A `comptime let NAME = <const-expr>;` is folded AT COMPILE TIME by the
// frontend (int literals, other comptime constants, parens, + - * / % with precedence) — no IL
// arithmetic is emitted for the expression; a read of NAME lowers to a single iconst, so the module
// carries only the folded constant. Comptime constants compose (B references A). No new IL/runtime.
// This harness instantiates the reactor + the sample against one shared linear memory, runs `main`,
// and asserts the output is BYTE-FOR-BYTE identical to the native proof (samples/comptime.tks).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout.
const expected = ["A = 42", "B = 50", "C = 10", "D = 2"];

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
const sample = await readFile(here("./samples/comptime.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL comptime:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   comptime: ${out.length} lines matched native byte-for-byte (compile-time constant folding)`);
process.exit(0);
