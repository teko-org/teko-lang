// Standalone-engine runner (Node / wasmtime-equivalent): loads each compiled WASM
// fixture and exercises its in-module entry export. Layer A needs no host
// runtime. Fixtures and their expected results:
//   channels.wasm  -> test() 42 (channel round-trip, Phase 10.1)
//   scheduler.wasm -> test() 15 (cooperative scheduler + spawn + blocking channel, 10.2)
//   emitted.wasm   -> main() 7  (REAL compiler output, Phase 10.2b): the Teko
//                    WASM emitter lowering a spawn + blocking-channel program —
//                    a green thread, dispatched via call_indirect, writes 7 into
//                    a shared channel that main() blocking-receives.
import { readFile } from "node:fs/promises";

const fixtures = [
  { file: "./samples/channels.wasm", expected: 42, name: "channels (10.1)", entry: "test" },
  { file: "./samples/scheduler.wasm", expected: 15, name: "scheduler (10.2)", entry: "test" },
  { file: "./samples/emitted.wasm", expected: 7, name: "emitted spawn+channel (10.2b)", entry: "main", optional: true },
];

let failures = 0;
for (const { file, expected, name, entry, optional } of fixtures) {
  try {
    let bytes;
    try {
      bytes = await readFile(new URL(file, import.meta.url));
    } catch (e) {
      if (optional) { console.log(`SKIP ${name}: ${file} not built`); continue; }
      throw e;
    }
    const { instance } = await WebAssembly.instantiate(bytes, {});
    const got = instance.exports[entry]();
    if (got !== expected) {
      console.error(`FAIL ${name}: ${entry}() = ${got}, expected ${expected}`);
      failures++;
    } else {
      console.log(`OK   ${name}: ${entry}() = ${got}`);
    }
  } catch (e) {
    console.error(`FAIL ${name}: ${e}`);
    failures++;
  }
}
process.exit(failures === 0 ? 0 : 1);
