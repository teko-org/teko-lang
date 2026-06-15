// Phase 13 — native name-based UUID proof (Node). The module under test is produced by the
// teko BINARY compiling runtime/wasm/samples/uuid.tks; uuid.v5/uuid.v3 run the in-module
// UUID runtime (SHA-1/MD5 over the DNS namespace || name, stamp + canonical format) — no host
// crypto. The host supplies env.emit, reads each NUL-terminated string, and checks RFC 4122.
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/uuid.wasm", import.meta.url));
let mem = null;
const got = [];
const dec = new TextDecoder();
const imports = {
  env: {
    emit: (ptr) => {
      const u = new Uint8Array(mem.buffer);
      let end = ptr >>> 0;
      while (u[end] !== 0) end++;
      got.push(dec.decode(u.subarray(ptr >>> 0, end)));
    },
  },
};
const { instance } = await WebAssembly.instantiate(wasm, imports);
mem = instance.exports.memory;
instance.exports.main();

const expected = [
  "886313e1-3b8a-5372-9b90-0c9aee199e5d", // uuid.v5(DNS, "python.org")
  "6fa459ea-ee8a-3ca4-894e-db77e160355e", // uuid.v3(DNS, "python.org")
];
if (JSON.stringify(got) === JSON.stringify(expected)) {
  console.log(`OK   uuid: ${got.length} RFC 4122 name-based vectors matched (native in-module, no host)`);
  process.exit(0);
} else {
  console.error(`FAIL uuid: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
