// Layer B runner worker (node worker_threads). Runs the module's main(), which
// blocks on memory.atomic.wait32 — that must happen OFF the node main thread, so
// the main thread stays free to bootstrap the producer worker (otherwise the
// blocked main thread can never start the worker -> deadlock). The module's SPAWN
// is brokered back to the parent, which starts the producer thread.
import { workerData, parentPort } from "node:worker_threads";

const { memory, bytes } = workerData;
const imports = {
  env: { memory },
  teko_rt: { spawn: (fn, arg) => parentPort.postMessage({ spawn: [fn, arg] }) },
};
const { instance } = await WebAssembly.instantiate(bytes, imports);
const got = instance.exports.main();   // blocks until the producer notifies
parentPort.postMessage({ result: got });
