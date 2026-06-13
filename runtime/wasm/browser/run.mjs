// Browser-side loader: instantiate the compiled WASM and run the channel
// round-trip in-page. The headless-browser harness (Playwright) reads
// `window.__tekoResult` to assert behavioral parity with Node/wasmtime.
// Also reports whether the page is crossOriginIsolated (COOP/COEP), which
// Layer B (SharedArrayBuffer + threads) will require.
const out = document.getElementById("out");
try {
  const res = await WebAssembly.instantiateStreaming(fetch("../samples/channels.wasm"), {});
  const got = res.instance.exports.test();
  window.__tekoResult = got;
  window.__crossOriginIsolated = self.crossOriginIsolated === true;
  out.textContent =
    `channel round-trip = ${got} (expected 42)\n` +
    `crossOriginIsolated = ${window.__crossOriginIsolated}`;
} catch (e) {
  window.__tekoResult = `error: ${e}`;
  out.textContent = `error: ${e}`;
}
