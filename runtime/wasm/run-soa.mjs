// Phase 18 (18.E.3) — SoA (structure-of-arrays) layout for an array-of-struct on the WASM target.
//
// soa.tks is compiled by the teko BINARY to a WASM module whose `soa Point[N]` construction, `s[i].field`
// read/write, `s.len`, and the whole-run accessor `s.field` lower to reactor entry points (OP_IARR_* ->
// teko_rt_iarray_*) — the SAME teko_iarray.c PACKED int32 store as native (compiled into crypto.wasm),
// sharing ONE linear memory. NO new opcode/runtime: SoA is pure FRONTEND lowering over the existing
// iarray runtime. The module's output must be BYTE-IDENTICAL to the native proof
// (runtime/native/samples/soa.tks), proving the SoA layout lowers consistently on both targets.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout.
const expected = ["s[1].x = 20", "s[2].y = 3", "len = 3", "sum_x = 60", "col.len = 3", "col[1] = 20"];

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
const sample = await readFile(here("./samples/soa.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL soa:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   soa: ${out.length} lines matched native byte-for-byte (SoA k contiguous i32 field runs: s[i].field r/w, s.len, whole-run s.field accessor, reactor-backed)`);
process.exit(0);
