// Phase 13 Sub-phase C — uuid.v4/v7-via-host proof (Node). The module under test is produced
// by the teko BINARY compiling runtime/wasm/samples/uuid_rng.tks; uuid.v4()/v7() lower to the
// in-module $teko_uuid_v4/$teko_uuid_v7 wrappers, which draw entropy from env.teko_random and
// (v7) a 48-bit Unix-ms timestamp from env.teko_now. This host fills entropy DETERMINISTICALLY
// (buf[i] = i & 0xff) and pins env.teko_now to a fixed timestamp, so the emitted UUIDs are
// exact KATs of the import ABI + version/variant nibble-setting + the in-module formatter.
// (In production the embedder supplies real CSPRNG bytes and the real wall clock.)
import { readFile } from "node:fs/promises";

const wasm = await readFile(new URL("./samples/uuid_rng.wasm", import.meta.url));
let mem = null;
const got = [];
const dec = new TextDecoder();
const imports = {
  env: {
    // Deterministic counter fill so the proof asserts exact bytes.
    teko_random: (ptr, len) => {
      const u = new Uint8Array(mem.buffer);
      for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = i & 0xff;
    },
    // Fixed 48-bit Unix-ms timestamp 0x0123456789ab so v7's time prefix is predictable.
    teko_now: () => 0x0123456789abn,
    emit: (ptr) => {
      const u = new Uint8Array(mem.buffer);
      let end = ptr >>> 0;
      while (u[end] !== 0) end++;
      got.push(dec.decode(u.subarray(ptr >>> 0, end)));
    },
  },
};
const { instance } = await WebAssembly.instantiate(wasm, imports);
mem = instance.exports.memory;
instance.exports.main();

// v4: random 00..0f, byte[6]=(06&0f)|40=46, byte[8]=(08&3f)|80=88.
// v7: bytes 0..5 = the 48-bit ms (01 23 45 67 89 ab), byte[6]=(06&0f)|70=76, byte[8]=88.
const expected = [
  "00010203-0405-4607-8809-0a0b0c0d0e0f",
  "01234567-89ab-7607-8809-0a0b0c0d0e0f",
];
if (JSON.stringify(got) === JSON.stringify(expected)) {
  console.log(`OK   uuid-rng: 2 vectors matched — uuid.v4/v7 via env.teko_random + env.teko_now host imports`);
  process.exit(0);
} else {
  console.error(`FAIL uuid-rng: got ${JSON.stringify(got)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
