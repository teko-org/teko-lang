// Phase 18.A — Zero-Overhead Optionals (`?T` nullability + `null` + the Elvis `??`) on the WASM
// target. optionals.tks is compiled by the teko BINARY to a WASM module. An optional local is
// COMPACTED: the payload in its $vN local + a hidden 1-word present companion; `a ?? d` branches on
// the present flag via WASM `(if … end)` (the same OP_IF lowering as native je/cbz), choosing the
// payload when present else the default. There is NO new IL/runtime — Elvis reuses OP_IF +
// local.get/set + i32.const. The 0/1 results are funneled through `convert.int_to_str` (id 49,
// reactor-backed) + `emit`. This harness instantiates the reactor + the sample against one shared
// linear memory, runs `main`, and asserts the output is BYTE-FOR-BYTE identical to the native proof
// (runtime/native/samples/optionals.tks) — proving optionals lower consistently on both targets.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

// Identical to the native proof's stdout (b = 7 / d = 5 / e = 5).
const expected = ["b = 7", "d = 5", "e = 5"];

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
const sample = await readFile(here("./samples/optionals.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

if (JSON.stringify(out) !== JSON.stringify(expected)) {
  console.error("FAIL optionals:");
  for (let i = 0; i < Math.max(out.length, expected.length); i++) {
    const g = out[i], w = expected[i];
    console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
  }
  process.exit(1);
}
console.log(`OK   optionals: ${out.length} lines matched native byte-for-byte (zero-overhead ?T/null/?? Elvis)`);
process.exit(0);
