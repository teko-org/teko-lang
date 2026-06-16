// Phase 15 (15.A) regression — method-call-as-argument on the WASM target. The teko binary
// compiles samples/method_arg.tks to a module whose OP_OBJ_* ops import teko_rt_object_* from the
// compiled-C runtime reactor (crypto.wasm) and share ONE linear memory with it — the SAME
// teko_object C runtime as the native runner (runtime/native/samples/method_arg.tks), not a second
// implementation. The fix under test: an `obj.method(args)` head used directly as an expression
// ARGUMENT / let-initializer / arithmetic operand now lowers to OP_CALL_FUNC (call_indirect) with
// `self` passed first, and a method returning a bare `self.<field>` carries the value (no iconst 0).
// This host instantiates the reactor + the sample against shared memory, captures env.log_int, and
// asserts [42, 42, 47, 142] — byte-identical to the native runner stdout.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  // The reactor imports these host hooks (crypto/time); the sample uses none — stub them.
  teko_now_ns: () => process.hrtime.bigint(),
  teko_now_unix: () => 1000000000n, teko_tz_offset: () => 0,
  teko_random: (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/method_arg.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
teko.instance.exports.main();

const expected = [42, 42, 47, 142]; // p.raw() arg / let / p.total() arg / p.raw()+100 operand
const ok = JSON.stringify(out) === JSON.stringify(expected);
if (ok) {
  console.log(`OK   method_arg(wasm): method call in argument/operand position -> ${JSON.stringify(out)} (OP_CALL_FUNC, bare self.field return)`);
  process.exit(0);
} else {
  console.error(`FAIL method_arg(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
