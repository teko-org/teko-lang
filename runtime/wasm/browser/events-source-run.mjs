// Browser-side loader for the FE-F event-handler-from-source proof. The module under
// test is produced by the teko BINARY compiling runtime/wasm/samples/events.tks
// (fn onClick(target){ @dom.setText(target,"…"); } + @dom.on(getElementById("count"),
// "click", onClick)). main() registers the listener; the harness clicks #count and the
// Teko handler routine (dispatched via teko_invoke) updates the DOM.
import { makeTekoDomImports } from "../samples/events.glue.mjs";

let memory, instance;
const imports = makeTekoDomImports(() => memory, () => instance);
try {
  const bytes = await (await fetch("../samples/events.wasm")).arrayBuffer();
  ({ instance } = await WebAssembly.instantiate(bytes, imports));
  memory = instance.exports.memory;
  instance.exports.main(); // registers the click listener
  window.__tekoEventsSource = { ok: true };
} catch (e) {
  window.__tekoEventsSource = { ok: false, error: String(e) };
}
