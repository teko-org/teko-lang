// Layer B runner worker (node worker_threads). Runs the module's main(), which
// blocks on memory.atomic.wait32 — that must happen OFF the node main thread, so
// the main thread stays free to bootstrap the producer worker. The module's SPAWN
// is brokered back to the parent, which starts the producer thread.
//
// DIAGNOSTICS: before running, stamp a sentinel into the shared memory. The
// producer reads it back; if the producer does NOT see the sentinel, the memory
// is not actually shared between the two workers (the root cause of a 60s
// "no notify" deadlock on some runners).
import { workerData, parentPort } from "node:worker_threads";

try {
  const { memory, bytes } = workerData;
  const isSAB = memory.buffer instanceof SharedArrayBuffer;
  // Sentinel at i32 index 25 (byte 100) — well clear of the channel region.
  new Int32Array(memory.buffer)[25] = 0xcafe;
  parentPort.postMessage({ log: `[runner] node=${process.version} shared=${isSAB} len=${memory.buffer.byteLength} wrote sentinel 0xcafe@i32[25]` });
  const imports = {
    env: { memory },
    teko_rt: { spawn: (fn, arg) => parentPort.postMessage({ spawn: [fn, arg] }) },
  };
  const { instance } = await WebAssembly.instantiate(bytes, imports);
  const got = instance.exports.main();   // blocks until the producer notifies
  parentPort.postMessage({ result: got });
} catch (e) {
  parentPort.postMessage({ error: String((e && e.stack) || e) });
}
