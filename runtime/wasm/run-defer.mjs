// Phase 18.C — `defer <stmt>;` scope-closing registration on the WASM target. defer.tks is compiled
// by the teko BINARY to a WASM module. Each `defer` registers a statement to run at `$main` close in
// LIFO order; the frontend captures the deferred statement's source and re-lexes + lowers it just
// before the `$main` close, so it runs AFTER the normal body, newest-first, and may reference locals
// (still live) incl. auto-`to_string` concat (id 49, reactor-backed). No new IL/runtime. This harness
// instantiates the reactor + the sample against one shared linear memory, runs `main`, and asserts
// the output is BYTE-FOR-BYTE identical to the native proof (samples/defer.tks).
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (immediate start/middle/end, then LIFO deferred).
const expected = ["start", "middle", "end", "last registered", "deferred n = 42"];

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
const sample = await readFile(here("./samples/defer.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL defer:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   defer: ${out.length} lines matched native byte-for-byte (LIFO scope-exit registration)`);
process.exit(0);
