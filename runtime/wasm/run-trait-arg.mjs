// Phase 15 (15.B) regression — trait-dispatch-as-argument on the WASM target. The teko binary
// compiles samples/trait_arg.tks to a module whose OP_VTABLE_* + OP_OBJ_* ops import
// teko_rt_vtable_* / teko_rt_object_* from the compiled-C runtime reactor (crypto.wasm) and share
// ONE linear memory with it — the SAME teko_vtable / teko_object C runtimes as the native runner
// (runtime/native/samples/trait_arg.tks), not a second implementation. The fix under test: a fat
// trait-typed `g.method()` head used directly as an expression ARGUMENT / arithmetic operand now
// lowers to vtable_get(type_id, method_id) -> routine slot -> OP_CALL_FUNC (call_indirect) in those
// positions too (previously only static `obj.method()` was handled there; the dynamic head dropped
// to iconst 0). This host instantiates the reactor + the sample against shared memory, captures
// env.log_int, and asserts [12, 112, 25] — byte-identical to the native runner stdout.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_now_ns: () => process.hrtime.bigint(),
  teko_now_unix: () => 1000000000n, teko_tz_offset: () => 0,
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/trait_arg.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [12, 112, 25]; // g.area() arg / g.area()+100 operand / Square.area after reassign
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   trait_arg(wasm): dynamic trait dispatch in argument/operand position -> ${JSON.stringify(out)} (vtable_get + OP_CALL_FUNC)`);
  process.exit(0);
} else {
  console.error(`FAIL trait_arg(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
