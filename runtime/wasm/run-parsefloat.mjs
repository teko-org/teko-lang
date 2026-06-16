// Phase 17 (17.E) — checked `convert.parse_float` (id 54, str->f64) on the WASM target, incl. the
// FAIL-LOUD path.
//
// parsefloat.tks / parsefloat_fail.tks are compiled by the teko BINARY to WASM modules. id 54 is the
// INVERSE of id 50's ABI: the reactor import is `(func $crypto_54 (param i32) (result f64))` and the
// call lowers `local.get $w0 / call $crypto_54 / local.set $f0` — string ptr in $w0, parsed double in
// $f0. The parser is the freestanding correctly-rounded teko_convert_parse_f64, EXPORTED from the
// compiled-C reactor (crypto.wasm) as teko_rt_parse_float — the SAME C source as native — so the happy
// module (which also formats via id 50, the 17.C Ryu core) is BYTE-IDENTICAL to the native proof
// (runtime/native/run-native.sh). The fail module parses "notafloat": the reactor's teko_rt_die calls
// __builtin_trap, so it must emit "before" then TRAP (the same value aborts non-zero on native) —
// proving the parse fails loudly, never silently returning 0.0. BOTH modules are reactor-backed (id
// 54/50), so both share env.memory with the reactor.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

function makeEnv(getMem, out) {
  return {
    teko_now_ns: () => process.hrtime.bigint(),
    teko_now_unix: () => 1000000000n,
    teko_tz_offset: () => 0,
    teko_random: (ptr, len) => {
      const u = new Uint8Array(getMem().buffer);
      for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = (i * 7 + 1) & 0xff;
    },
    emit: (ptr) => {
      const u = new Uint8Array(getMem().buffer);
      let e = ptr >>> 0; while (u[e] !== 0) e++;
      out.push(dec.decode(u.subarray(ptr >>> 0, e)));
    },
  };
}

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
let ok = true;

// 1) Happy path — reactor-backed (id 54 parse + id 50 format), shares env.memory; byte-identical
//    to the native proof. Proves parse->format round-trips and the parse result auto-to_string's.
{
  const memory = new WebAssembly.Memory({ initial: 64 });
  const out = [];
  const env = { memory, ...makeEnv(() => memory, out) };
  const reactor = await WebAssembly.instantiate(reactorBytes, { env });
  const sample = await readFile(here("./samples/parsefloat.wasm"));
  const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
  teko.instance.exports.main();
  const expected = ["3.14", "got 0.5", "3.0"];
  if (JSON.stringify(out) !== JSON.stringify(expected)) {
    console.error("FAIL parsefloat (happy):");
    for (let i = 0; i < Math.max(out.length, expected.length); i++)
      console.error(`  [${i}] ${out[i] === expected[i] ? "ok  " : "DIFF"} got=${out[i]} want=${expected[i]}`);
    ok = false;
  } else {
    console.log(`OK   parsefloat: ${out.length} lines matched native byte-for-byte (parse->format round-trip + auto-to_string)`);
  }
}

// 2) Fail-loud path — parsing "notafloat" must TRAP (not silently return 0.0). Reactor-backed (id 54),
//    so it shares env.memory; teko_rt_die -> __builtin_trap in the reactor.
{
  const memory = new WebAssembly.Memory({ initial: 64 });
  const out = [];
  const env = { memory, ...makeEnv(() => memory, out) };
  const reactor = await WebAssembly.instantiate(reactorBytes, { env });
  const sample = await readFile(here("./samples/parsefloat_fail.wasm"));
  const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
  let trapped = false;
  try { teko.instance.exports.main(); }
  catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
  if (!trapped) {
    console.error(`FAIL parsefloat_fail: expected a trap (fail-loud), but main() returned. emitted=${JSON.stringify(out)}`);
    ok = false;
  } else if (JSON.stringify(out) !== JSON.stringify(["before"])) {
    console.error(`FAIL parsefloat_fail: expected stdout ["before"] before the trap, got ${JSON.stringify(out)}`);
    ok = false;
  } else {
    console.log(`OK   parsefloat_fail: emitted ["before"] then TRAPPED on malformed input (fail-loud)`);
  }
}

process.exit(ok ? 0 : 1);
