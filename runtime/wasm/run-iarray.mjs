// Phase 18 (18.E.2) — the TYPED `i32[]` PACKED numeric array (the SIMD substrate) on the WASM target.
//
// iarray.tks is compiled by the teko BINARY to a WASM module whose typed-array literal / index r-w /
// `.len` / `for x in a` lower to reactor entry points (OP_IARR_* -> teko_rt_iarray_*) — the SAME
// teko_iarray.c PACKED int32 store as native (compiled into crypto.wasm), sharing ONE linear memory.
// The module's output must be BYTE-IDENTICAL to the native proof (runtime/native/samples/iarray.tks),
// proving the typed packed-array substrate lowers consistently on both targets.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout.
const expected = ["a[2] = 6", "len = 3", "sum = 15", "a[0] = 40"];

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
const sample = await readFile(here("./samples/iarray.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL iarray:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   iarray: ${out.length} lines matched native byte-for-byte (typed i32[] packed array: literal/index/.len/for, reactor-backed)`);
process.exit(0);
