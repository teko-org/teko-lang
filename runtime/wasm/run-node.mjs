// Standalone-engine runner (Node / wasmtime-equivalent): loads the compiled WASM
// and exercises the in-module channel round-trip. Layer A needs no host runtime.
//   usage: node run-node.mjs [path-to-wasm]   (default: samples/channels.wasm)
import { readFile } from "node:fs/promises";

const wasmPath = new URL(process.argv[2] ?? "./samples/channels.wasm", import.meta.url);
const bytes = await readFile(wasmPath);
const { instance } = await WebAssembly.instantiate(bytes, {});
const got = instance.exports.test();
const expected = 42;
if (got !== expected) {
  console.error(`FAIL: channel round-trip returned ${got}, expected ${expected}`);
  process.exit(1);
}
console.log(`OK: channel round-trip = ${got}`);
