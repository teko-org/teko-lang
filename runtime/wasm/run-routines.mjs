// Phase 14 (14.A) executable proof (Node): the module under test is the real `teko`
// binary compiling runtime/wasm/samples/routines.tks. `routines { worker(); ×3 }` fires
// three background tasks; the cooperative scheduler is drained at `$main` close so they
// run AFTER `$main`'s body. This host supplies env.log, records the call order, and asserts
// the three "worker ran" calls land AFTER both "main start" and "main end" — i.e. the tasks
// were genuinely deferred to the scheduler, not invoked inline.
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/routines.wasm", import.meta.url));
let mem = null;
const calls = [];
const imports = {
  env: {
    log: (ptr) => {
      const u = new Uint8Array(mem.buffer);
      let end = ptr >>> 0;
      while (u[end] !== 0) end++;
      calls.push(new TextDecoder().decode(u.subarray(ptr >>> 0, end)));
    },
  },
};
const { instance } = await WebAssembly.instantiate(wasm, imports);
mem = instance.exports.memory;
instance.exports.main();

const expected = ["main start", "main end", "worker ran", "worker ran", "worker ran"];
const ok = calls.length === expected.length && calls.every((c, i) => c === expected[i]);
if (ok) {
  console.log(`OK   routines: 3 background tasks ran after main body — order ${JSON.stringify(calls)}`);
  process.exit(0);
} else {
  console.error(`FAIL routines: got ${JSON.stringify(calls)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
