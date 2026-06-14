// Browser-side loader for the Browser FFI MVP-3 events proof. Instantiates the
// compiler-emitted module against the AUTO-GENERATED glue and runs $main, which
// registers a click listener on #count bound to a Teko callback (table slot 0).
// When the harness clicks #count, the glue calls exports.teko_invoke(0, handle),
// dispatching the Teko routine that sets the element's text to "clicked!".
import { makeTekoDomImports } from "../samples/emitted_events.glue.mjs";

let memory, instance;
const imports = makeTekoDomImports(() => memory, () => instance);
try {
  const bytes = await (await fetch("../samples/emitted_events.wasm")).arrayBuffer();
  ({ instance } = await WebAssembly.instantiate(bytes, imports));
  memory = instance.exports.memory;
  instance.exports.main(); // registers the listener; does NOT fire the callback
  window.__tekoEvents = { ok: true, before: document.getElementById("count").textContent };
} catch (e) {
  window.__tekoEvents = { ok: false, error: String(e) };
}
