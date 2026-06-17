// Phase 18 (18.E.4) — REAL simd128 SIMD reduction (`simd.sum`) on the WASM target.
//
// simd.tks is compiled by the teko BINARY to a WASM module whose `simd.sum(run)` lowers to an IN-MODULE
// `$teko_simd_sum_i32` kernel — a REAL `v128.load` + `i32x4.add` accumulate (4-wide) + lane collapse +
// scalar tail — over the run's data pointer (an i32 offset into the SHARED linear memory; fetched via
// the reactor's teko_rt_iarray_data). UNLIKE 18.E.1–.3 the vectorized instruction stream DIFFERS from
// native (SSE2/NEON), so the proof is by VALUE: the emitted stdout must be BYTE-IDENTICAL to the native
// proof (runtime/native/samples/simd.tks), AND the in-program scalar self-check (simd == a `for` loop)
// must hold — a mis-emitted vector op makes a line differ and FAILS here.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (integers — ISA-independent despite different vector streams).
const expected = ["simd = 55", "scalar = 55", "soa_simd = 210", "soa_scalar = 210"];

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
const sample = await readFile(here("./samples/simd.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL simd:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   simd: ${out.length} lines matched native byte-for-byte (REAL simd128 v128.load/i32x4.add kernel + in-program scalar self-check; SoA field run vectorized)`);
process.exit(0);
