// Phase 16 (16.F) — CHECKED string->primitive parse on the WASM target, incl. the FAIL-LOUD path.
//
// parse.tks / parse_fail.tks are compiled by the teko BINARY to WASM modules whose
// convert.parse_int/parse_bool calls lower to reactor entry points (OP_CALL_RUNTIME ids 53/55) —
// the SAME checked teko_convert.c core as native (crypto.wasm), sharing ONE linear memory. The
// happy module's output must be byte-identical to the native proof; the fail module must TRAP
// (the reactor calls __builtin_trap on malformed input → a WebAssembly.RuntimeError the host sees),
// proving the conversion fails loudly rather than silently truncating.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();

function makeEnv(memory, out) {
  return {
    memory,
    teko_now_ns: () => process.hrtime.bigint(),
    teko_now_unix: () => 1000000000n,
    teko_tz_offset: () => 0,
    teko_random: (ptr, len) => {
      const u = new Uint8Array(memory.buffer);
      for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = (i * 7 + 1) & 0xff;
    },
    emit: (ptr) => {
      const u = new Uint8Array(memory.buffer);
      let e = ptr >>> 0; while (u[e] !== 0) e++;
      out.push(dec.decode(u.subarray(ptr >>> 0, e)));
    },
  };
}

async function instantiate(reactorBytes, sampleName, memory, out) {
  const env = makeEnv(memory, out);
  const reactor = await WebAssembly.instantiate(reactorBytes, { env });
  const sample = await readFile(here(`./samples/${sampleName}.wasm`));
  const teko = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports });
  return teko.instance;
}

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
let ok = true;

// 1) Happy path — byte-identical to the native proof.
{
  const expected = ["n = 123", "neg = -42", "ws = 7", "true", "false"];
  const out = [];
  const inst = await instantiate(reactorBytes, "parse", new WebAssembly.Memory({ initial: 64 }), out);
  inst.exports.main();
  if (JSON.stringify(out) !== JSON.stringify(expected)) {
    console.error("FAIL parse (happy):");
    for (let i = 0; i < Math.max(out.length, expected.length); i++)
      console.error(`  [${i}] ${out[i] === expected[i] ? "ok  " : "DIFF"} got=${out[i]} want=${expected[i]}`);
    ok = false;
  } else {
    console.log(`OK   parse: ${out.length} checked parses matched (reactor-backed)`);
  }
}

// 2) Fail-loud path — a malformed integer must TRAP (not silently return 0).
{
  const out = [];
  const inst = await instantiate(reactorBytes, "parse_fail", new WebAssembly.Memory({ initial: 64 }), out);
  let trapped = false;
  try { inst.exports.main(); }
  catch (e) { trapped = e instanceof WebAssembly.RuntimeError; }
  if (!trapped) {
    console.error(`FAIL parse_fail: expected a trap (fail-loud), but main() returned. emitted=${JSON.stringify(out)}`);
    ok = false;
  } else if (JSON.stringify(out) !== JSON.stringify(["before"])) {
    console.error(`FAIL parse_fail: expected stdout ["before"] before the trap, got ${JSON.stringify(out)}`);
    ok = false;
  } else {
    console.log(`OK   parse_fail: emitted ["before"] then TRAPPED on malformed input (fail-loud)`);
  }
}

process.exit(ok ? 0 : 1);
