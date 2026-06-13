// Layer B producer worker (node worker_threads, a real OS thread). Re-instantiate
// the SAME module against the SAME shared memory and run one routine via the
// module's exported teko_invoke(fn, arg) dispatcher, publishing its value through
// the atomic channel to wake the runner's blocked main().
import { workerData, parentPort } from "node:worker_threads";

const { memory, bytes, fn, arg } = workerData;
const imports = { env: { memory }, teko_rt: { spawn: () => {} } };
const { instance } = await WebAssembly.instantiate(bytes, imports);
instance.exports.teko_invoke(fn, arg);
parentPort.postMessage("done");
