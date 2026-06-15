// Phase 13.1 — native SHA-256 proof (Node). The module under test is produced by the teko
// BINARY compiling runtime/wasm/samples/hash.tks; it calls the native in-module SHA-256
// runtime (no external deps, no host crypto). The host supplies env.emit, reads each
// NUL-terminated hex digest from exported memory, and checks the FIPS 180-4 vectors.
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/hash.wasm", import.meta.url));
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
  "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", // SHA-256("abc")
  "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", // SHA-256("")
  "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592", // SHA-256("The quick brown fox jumps over the lazy dog")
];
if (JSON.stringify(got) === JSON.stringify(expected)) {
  console.log(`OK   sha256: ${got.length} FIPS 180-4 vectors matched (native in-module, no host crypto)`);
  process.exit(0);
} else {
  console.error(`FAIL sha256: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
