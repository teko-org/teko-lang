// Phase 14 (14.C, real-time clock) — delayed (timed) channel on the WASM target. The teko binary
// compiles samples/delayed.tks to a module whose OP_DELAYED_* import teko_rt_delayed_* from the
// compiled-C reactor (crypto.wasm); the reactor reads the host MONOTONIC clock (env.teko_now_ns)
// and shares ONE linear memory — the SAME delayed C runtime as native. Because the time base is
// REAL (not a logical clock), this asserts the real-DEADLINE release ORDER (timing-robust — recv
// returns the earliest-due) + a LOWER BOUND on elapsed time, not exact counters.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
const env = {
  memory,
  teko_now_ns: () => process.hrtime.bigint(),  // real monotonic ns (reactor's delayed clock)
  teko_random: (ptr, len) => { const u = new Uint8Array(memory.buffer); for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0; },
  log_int: (n) => { out.push(n | 0); },
};

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample = await readFile(here("./samples/delayed.wasm"));
const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
const t0 = process.hrtime.bigint();
teko.instance.exports.main();
const elapsedMs = Number(process.hrtime.bigint() - t0) / 1e6;

// Sent 10@2ms, 20@6ms, 30@4ms -> real-deadline order is 10, 30, 20; last due at ~6ms.
const okOrder = JSON.stringify(out) === JSON.stringify([10, 30, 20]);
const okTime  = elapsedMs >= 4; // last deadline ~6ms; lower bound 4ms (clock tolerance)
if (okOrder && okTime) {
  console.log(`OK   delayed(wasm): real-deadline order ${JSON.stringify(out)}, elapsed ${elapsedMs.toFixed(1)}ms (>= 4ms, real clock, reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL delayed(wasm): got ${JSON.stringify(out)} (want [10,30,20]), elapsed=${elapsedMs.toFixed(1)}ms (want >= 4ms)`);
  process.exit(1);
}
