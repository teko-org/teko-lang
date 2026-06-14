// Diagnostic observer (node worker_threads): independently polls the shared memory
// and logs the timeline of the channel cell + the runner's sentinel. This is a
// THIRD party (neither the runner nor the producer), so it reveals whether the
// producer's flag/value store actually propagates into the shared buffer that
// every worker holds — decoupled from the consumer's wasm. If the observer sees
// flag 0 -> 1 but the consumer's main() still hangs, the consumer's wasm reads a
// different buffer; if the observer never sees it, the store doesn't propagate.
import { workerData, parentPort } from "node:worker_threads";

const { memory, flagIdx } = workerData;
const i32 = new Int32Array(memory.buffer);
let lastFlag = -1;
let ticks = 0;
const timer = setInterval(() => {
  const flag = Atomics.load(i32, flagIdx);
  const value = Atomics.load(i32, flagIdx + 1);
  const sentinel = Atomics.load(i32, 25);
  if (flag !== lastFlag) {
    parentPort.postMessage({ log: `[observer] t=${ticks * 100}ms shared=${memory.buffer instanceof SharedArrayBuffer} sentinel=0x${sentinel.toString(16)} flag i32[${flagIdx}]=${flag} value i32[${flagIdx + 1}]=${value}` });
    lastFlag = flag;
  }
  if (++ticks > 50) { clearInterval(timer); parentPort.postMessage({ log: "[observer] done (5s)" }); }
}, 100);
