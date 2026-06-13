// Layer B producer worker (node worker_threads, a real OS thread). Re-instantiate
// the SAME module against the SAME shared memory and run one routine via the
// module's exported teko_invoke(fn, arg) dispatcher, publishing its value through
// the atomic channel to wake the runner's blocked main().
//
// DIAGNOSTICS: read the sentinel the runner stamped. If we do NOT see 0xcafe, this
// worker got a DIFFERENT memory than the runner — the cross-thread notify will
// never reach the waiter. Also report the flag/value cells after publishing.
import { workerData, parentPort } from "node:worker_threads";

try {
  const { memory, bytes, fn, arg } = workerData;
  const i32 = new Int32Array(memory.buffer);
  const isSAB = memory.buffer instanceof SharedArrayBuffer;
  const sentinel = Atomics.load(i32, 25);
  parentPort.postMessage({ log: `[producer] node=${process.version} shared=${isSAB} len=${memory.buffer.byteLength} sees sentinel i32[25]=0x${sentinel.toString(16)} (expect cafe => same memory)` });
  const imports = { env: { memory }, teko_rt: { spawn: () => {} } };
  const { instance } = await WebAssembly.instantiate(bytes, imports);
  instance.exports.teko_invoke(fn, arg);
  // The channel lives at byte `arg` (flag) / `arg`+4 (value) => i32 indices.
  const flagIdx = arg >> 2;
  parentPort.postMessage({ log: `[producer] after invoke: flag i32[${flagIdx}]=${Atomics.load(i32, flagIdx)} value i32[${flagIdx + 1}]=${Atomics.load(i32, flagIdx + 1)}` });
  parentPort.postMessage({ done: true });
} catch (e) {
  parentPort.postMessage({ error: String((e && e.stack) || e) });
}
