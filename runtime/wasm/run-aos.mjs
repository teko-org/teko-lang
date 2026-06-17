// Phase 18 (18.E.3) — AoS (array-of-objects) layout, the contrast to SoA, on the WASM target.
//
// aos.tks is compiled by the teko BINARY to a WASM module whose `[Point(), …]` object-array literal and
// `a[i].field` index-then-member access lower to reactor entry points (OP_ARR_* -> teko_rt_array_*,
// OP_OBJ_* -> teko_rt_object_*) — the SAME teko_array.c / teko_object.c stores as native (compiled into
// crypto.wasm), sharing ONE linear memory. NO new opcode/runtime: AoS is pure FRONTEND lowering. The
// module's output must be BYTE-IDENTICAL to the native proof (runtime/native/samples/aos.tks) and gives
// the SAME logical result as the SoA proof (sum of x = 60), proving the two layouts are interchangeable.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout.
const expected = ["a[1].x = 20", "a[2].y = 3", "len = 3", "sum_x = 60"];

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
const sample = await readFile(here("./samples/aos.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL aos:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   aos: ${out.length} lines matched native byte-for-byte (AoS array of object handles: a[i].field index-then-member, reactor-backed)`);
process.exit(0);
