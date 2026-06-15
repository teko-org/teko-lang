// Phase 11 Browser FFI FE-D — end-to-end SOURCE→WASM proof. The module under test is
// produced by the real `teko` binary compiling runtime/wasm/samples/hello.tks
// (extern fn log … from "env" as "log"; log("hello from teko source")) — NOT an
// emit-demo. This host supplies env.log, reads the NUL-terminated pooled string back
// from exported linear memory, and asserts the Teko program called it with the literal.
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/hello.wasm", import.meta.url));
let mem = null;
let captured = null;
const imports = {
  env: {
    log: (ptr) => {
      const u = new Uint8Array(mem.buffer);
      let end = ptr >>> 0;
      while (u[end] !== 0) end++;
      captured = new TextDecoder().decode(u.subarray(ptr >>> 0, end));
    },
  },
};
const { instance } = await WebAssembly.instantiate(wasm, imports);
mem = instance.exports.memory;
instance.exports.main();

const expected = "hello from teko source";
if (captured === expected) {
  console.log(`OK   source→wasm: teko-compiled hello.tks called env.log("${captured}")`);
  process.exit(0);
} else {
  console.error(`FAIL source→wasm: captured ${JSON.stringify(captured)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
