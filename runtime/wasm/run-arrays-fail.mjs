// Phase 18 (18.E.1) — the FAIL-LOUD array bounds check on the WASM target.
//
// arrays_fail.tks is compiled by the teko BINARY to a WASM module whose out-of-range access lowers to
// the reactor's checked teko_rt_array_get — which calls teko_rt_die's __builtin_trap on an OOB index,
// surfacing a WebAssembly.RuntimeError the host sees. The module must emit "before" then TRAP, proving
// the access fails loudly (native exit 70) rather than silently returning 0 / corrupting memory.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

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
    let e = ptr >>> 0; while (u[e] !== 0) e++;
    out.push(dec.decode(u.subarray(ptr >>> 0, e)));
  },
};

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
const reactor = await WebAssembly.instantiate(reactorBytes, { env });
const sample = await readFile(here("./samples/arrays_fail.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });

let trapped = false;
try { teko.instance.exports.main(); }
catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
if (!trapped) {
  console.error(`FAIL arrays_fail: expected a trap (fail-loud), but main() returned. emitted=${JSON.stringify(out)}`);
  process.exit(1);
}
if (JSON.stringify(out) !== JSON.stringify(["before"])) {
  console.error(`FAIL arrays_fail: expected stdout ["before"] before the trap, got ${JSON.stringify(out)}`);
  process.exit(1);
}
console.log(`OK   arrays_fail: emitted ["before"] then TRAPPED on an out-of-bounds index (fail-loud)`);
process.exit(0);
