// Browser-side loader for the Browser FFI MVP-2 DOM proof. Instantiates the
// compiler-emitted module against the AUTO-GENERATED glue (makeTekoDomImports),
// runs $main, and exposes the outcome on window.__tekoDom for the Playwright
// harness (run-dom.mjs) to assert. main() builds a <span>"hello from teko" and
// appends it to #out entirely through the dom.* host imports.
import { makeTekoDomImports } from "../samples/emitted_dom.glue.mjs";

let memory, instance;
const imports = makeTekoDomImports(() => memory, () => instance);
try {
  const bytes = await (await fetch("../samples/emitted_dom.wasm")).arrayBuffer();
  ({ instance } = await WebAssembly.instantiate(bytes, imports));
  memory = instance.exports.memory;
  instance.exports.main();
  const span = document.querySelector("#out > span");
  window.__tekoDom = { ok: true, text: span ? span.textContent : null };
} catch (e) {
  window.__tekoDom = { ok: false, error: String(e) };
}
