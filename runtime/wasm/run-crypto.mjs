// Phase 13 Sub-phase C ("big step") — full crypto language surface on the WASM target.
//
// Each crypto_*.tks is compiled by the teko BINARY to a WASM module that lowers the
// hash/HMAC/AEAD/KDF/signature surface (OP_CALL_RUNTIME ids 5,10-40) to IMPORTED entry
// points in the compiled-C crypto reactor (crypto.wasm). The reactor is the single C
// runtime (src/runtime/teko_crypto_*.c) compiled to wasm32; both modules share ONE linear
// memory (host-owned). This harness instantiates the reactor + each sample against that
// shared memory and asserts the emitted output against the SAME FIPS/NIST/RFC KAT vectors
// the native proofs use (runtime/native/run-native.sh) — proving WASM lowers to the same
// implementation as native, not a second one.
//
// Entropy: the reactor imports env.teko_random; RSA PSS/OAEP draw a random salt/seed from
// it. The proofs assert round-trip outcomes (sign->verify, encrypt->decrypt), so a
// deterministic counter fill suffices and keeps the KAT exact.
import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec = new TextDecoder();
const enc = new TextEncoder();

const GROUPS = [
  { name: "crypto_hash", expected: [
    "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7",
    "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
    "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532",
    "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0",
    "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262",
    "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d17d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923",
    "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26",
    "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f",
  ]},
  { name: "crypto_hmac", expected: [
    "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
    "af45d2e376484031617f78d2b58a6b1b9c7ef464f5a01b47e42ec3736322445e8e2240ca5e69e2c78b3239ecfab21649",
    "164b7a7bfcf819e2e395fbe73b56e0a387bd64222e831fd610270cd7ea2505549758bf75c05a994a6d034f65f8f0e6fdcaeab1a34d4a6b4b636e070a38bce737",
  ]},
  { name: "crypto_aead", expected: [
    "42831ec2217774244b7221b784d0d49ce3aa212f2c02a4e035c17e2329aca12e21d514b25466931c7d8f6a5aac84aa051ba30b396a0aac973d58e091473f59854d5c2af327cd64a62cf35abd2ba6fab4",
    "d9313225f88406e5a55909c5aff5269a86a7a9531534f7da2e4c303d8a318a721c3c0c95956809532fcf0e2449a6b525b16aedf5aa0de657ba637b391aafd255",
    "REJECT",
    "d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691",
    "4c616469657320616e642047656e746c656d656e206f662074686520636c617373206f66202739393a204966204920636f756c64206f6666657220796f75206f6e6c79206f6e652074697020666f7220746865206675747572652c2073756e73637265656e20776f756c642062652069742e",
  ]},
  { name: "crypto_sign", expected: [
    "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a",
    "1",
    "0",
    "c3da55379de9c6908e94ea4df28d084f32eccf03491c71f754b4075577a28552",
    "95cbde9476e8907d7aade45cb4b873f88b595a68799fa152e6f8f7647aac7957",
    "efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8",
    "1",
    "0",
    "94edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d64c4ea95ad133c81a648152e44acf96e36dd1e80fabe4699ef4aeb15f178cea1fe40db2603138f130e740a19624526203b6351d0a3a94fa329c145786e679e7b82c71a38628ac8",
    "1",
    "0",
  ]},
  { name: "crypto_kdf", expected: [
    "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",
    "55ac046e56e3089fec1691c22544b605f94185216dde0465e68b9d57c20dacbc49ca9cccf179b645991664b39d77ef317c71b845b1e30bd509112041d3a19783",
  ]},
  { name: "crypto_rsa", expected: [
    "1",
    "0",
    "48656c6c6f2c2052534121",
  ]},
];

async function runGroup(reactorBytes, group) {
  // Fresh memory per group so state never leaks across samples.
  const memory = new WebAssembly.Memory({ initial: 64 });
  const out = [];
  const env = {
    memory,
    // Deterministic counter fill — exact KATs for deterministic primitives; for RSA's
    // random salt/seed only the round-trip outcome is asserted, so any fill works.
    teko_random: (ptr, len) => {
      const u = new Uint8Array(memory.buffer);
      for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = (i * 7 + 1) & 0xff;
    },
    emit: (ptr) => {
      const u = new Uint8Array(memory.buffer);
      let e = ptr >>> 0;
      while (u[e] !== 0) e++;
      out.push(dec.decode(u.subarray(ptr >>> 0, e)));
    },
  };
  const reactor = await WebAssembly.instantiate(reactorBytes, { env });
  const sample = await readFile(here(`./samples/${group.name}.wasm`));
  const teko = await WebAssembly.instantiate(sample, {
    env,
    crypto: reactor.instance.exports,
  });
  teko.instance.exports.main();

  if (JSON.stringify(out) !== JSON.stringify(group.expected)) {
    console.error(`FAIL ${group.name}:`);
    for (let i = 0; i < Math.max(out.length, group.expected.length); i++) {
      const g = out[i], w = group.expected[i];
      console.error(`  [${i}] ${g === w ? "ok  " : "DIFF"} got=${g} want=${w}`);
    }
    return false;
  }
  console.log(`OK   ${group.name}: ${out.length} KAT vectors matched (reactor-backed)`);
  return true;
}

const reactorBytes = await readFile(here("./crypto/crypto.wasm"));
let ok = true;
for (const g of GROUPS) ok = (await runGroup(reactorBytes, g)) && ok;
if (ok) {
  console.log(`\nAll crypto groups passed — full hash/HMAC/AEAD/KDF/signature surface works on WASM via the compiled-C reactor.`);
  process.exit(0);
}
process.exit(1);
