// Phase 18 (18.E.2) — `for NAME in ARR { body }` iteration over an i64 `array` on the WASM target.
//
// foreach.tks is compiled by the teko BINARY to a WASM module whose `for` loop lowers to the
// control-flow foundation (LOOP_BEGIN/BREAK_IF_FALSE/LOOP_END) walking `[0, arr.len)`, binding the
// loop var to each element via OP_ARR_GET — the SAME teko_array.c contiguous store as native
// (compiled into crypto.wasm), sharing ONE linear memory. The module's output must be BYTE-IDENTICAL
// to the native proof (runtime/native/samples/foreach.tks), proving `for..in` lowers consistently.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout.
const expected = ["sum = 60"];

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
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
const sample = await readFile(here("./samples/foreach.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL foreach:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   foreach: ${out.length} line(s) matched native byte-for-byte (for x in array, control-flow foundation)`);
process.exit(0);
