// Phase 16 (16.C) — string INTERPOLATION with auto-`to_string` on the WASM target.
//
// interp.tks is compiled by the teko BINARY to a WASM module whose `"…{expr}…"` literals lower to
// reactor-backed to_string (OP_CALL_RUNTIME id 49) + str_concat (id 52) — the SAME teko_convert.c
// source of truth as native (compiled into crypto.wasm), sharing ONE host-owned linear memory.
// This harness asserts the emitted output is byte-for-byte identical to the native proof
// (runtime/native/run-native.sh), proving interpolation lowers the same way on both targets.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

const expected = ["x = 42", "42 items, 42 total", "sum = 50", "count: 42", "braces { } kept", "[42]"];

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor links the whole runtime, so it imports these host facts even though interp uses
  // none of them — stub so it instantiates.
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
const sample = await readFile(here("./samples/interp.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL interp:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   interp: ${out.length} auto-to_string interpolations matched (reactor-backed)`);
process.exit(0);
