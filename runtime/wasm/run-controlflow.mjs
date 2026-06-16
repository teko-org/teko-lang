// Phase 14 (control-flow foundation) — structured loops + branches on the WASM target. The teko
// binary compiles samples/controlflow.tks to a module whose OP_LOOP_*/OP_IF_*/OP_BREAK* lower to
// WASM structured (block $brk (loop $cont …)) + (if … end). This host captures env.log_int and
// asserts the while-loop sum (0..4 = 10) and the loop/break/continue counter (5). No reactor: the
// module owns its memory.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const out = [];
const env = { log_int: (n) => { out.push(n | 0); } };

const sample = await readFile(here("./samples/controlflow.wasm"));
const teko = await WebAssembly.instantiate(sample, { env });
teko.instance.exports.main();

const ok = JSON.stringify(out) === JSON.stringify([10, 5]);
if (ok) {
  console.log(`OK   controlflow(wasm): while-sum + loop/if/break/continue -> ${JSON.stringify(out)}`);
  process.exit(0);
} else {
  console.error(`FAIL controlflow(wasm): got ${JSON.stringify(out)}, expected [10,5]`);
  process.exit(1);
}
