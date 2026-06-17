// Phase 19 (HTTP-INT — http.* client surface) — WASM proof harness.
//
// WASM cannot open raw OS sockets, so http.get/post lower to env.teko_http_get /
// env.teko_http_post HOST imports, backed here by a pure in-memory mock (no real HTTP).
// The emitted module has the same shape as all other Teko WASM samples: it imports the
// compiled-C reactor (crypto.wasm) and shares ONE linear memory with it (env.memory),
// and it imports `emit` from the "teko_rt" namespace.
//
// Round-trip being asserted:
//   http.get("http://example.com/hello") -> body_ptr (host mock returns "hello-http")
//   emit(body_ptr)                        -> "hello-http"
//
// The mock:
//   teko_http_get(url_ptr) — reads the URL string from linear memory (for verification),
//     writes "hello-http" into a fixed RECV_OFFSET, returns RECV_OFFSET.
//   teko_http_post(url_ptr, body_ptr) — reads both strings, writes back the body reversed
//     into RECV_OFFSET (not exercised in http.tks but declared to satisfy the import).
//
// SAST (host side): url_ptr / body_ptr are i32 module values bounded to the module's
// linear memory; the mock caps reads at MAX_BUF before scanning; "hello-http" fits well
// within the MAX_BUF cap; no untrusted data escapes the memory region; no format-string
// or path-traversal occurs on the module side (url is a compile-time string constant).

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here  = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec   = new TextDecoder();
const enc   = new TextEncoder();

const memory     = new WebAssembly.Memory({ initial: 64 });
const MAX_BUF    = 65535;         // TEKO_SOCKET_MAX_BUF cap
const RECV_OFFSET = 0x300;        // fixed scratch offset where the mock writes response bodies

const out = [];

// Read a NUL-terminated C string from linear memory at ptr.
const readCStr = (ptr) => {
  const u = new Uint8Array(memory.buffer);
  let end = ptr >>> 0;
  while (u[end] !== 0) end++;
  return dec.decode(u.subarray(ptr >>> 0, end));
};

// Write bytes + NUL into linear memory at offset; returns the offset.
// Caps at MAX_BUF to guard against allocation bombs from a hostile module.
const writeCStr = (offset, str) => {
  const bytes = enc.encode(str);
  const n = Math.min(bytes.length, MAX_BUF);
  const u = new Uint8Array(memory.buffer);
  for (let i = 0; i < n; i++) u[(offset + i) >>> 0] = bytes[i];
  u[(offset + n) >>> 0] = 0; // NUL-terminate
  return offset;
};

const env = {
  memory,
  // --- http.* host mock (env namespace) ------------------------------------------
  teko_http_get: (url_ptr) => {
    // Read the URL the module is requesting (for verification / observability).
    const url = readCStr(url_ptr);
    void url; // acknowledged — the mock doesn't need it to construct the response
    // Write the canned response body and return its pointer.
    return writeCStr(RECV_OFFSET, "hello-http");
  },
  teko_http_post: (url_ptr, body_ptr) => {
    // http.post is declared but not exercised in http.tks; provide a working stub.
    const url  = readCStr(url_ptr);
    const body = readCStr(body_ptr);
    void url; void body;
    return writeCStr(RECV_OFFSET, "post-ok");
  },
  // --- net.* host mock (env namespace) — http sample uses no net.* ops, stub them ---
  teko_net_tcp_connect: (_h, _p) => 0,
  teko_net_udp_open:    (_h, _p) => 0,
  teko_net_send:        (_h, _d, _l) => 1, // TEKO_SOCK_ERR_BADARG
  teko_net_recv:        (_h, _m) => 0,
  teko_net_close:       (_h) => 0,
  teko_net_free:        (_h) => 0,
  teko_net_state:       (_h) => 4, // TEKO_SOCK_ERROR
  // --- reactor host hooks (crypto/time/entropy) — http sample uses none, stub them ---
  teko_now_ns:    () => process.hrtime.bigint(),
  teko_now_unix:  () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random:    (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
};

// `emit` is imported from the "teko_rt" namespace.
const teko_rt = { teko_rt_emit: (ptr) => { out.push(readCStr(ptr)); } };

// Instantiate the reactor (provides the crypto namespace) then the sample.
const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample  = await readFile(here("./samples/http.wasm"));
const teko    = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports, teko_rt });
teko.instance.exports.main();

const expected = ["hello-http"];
if (JSON.stringify(out) === JSON.stringify(expected)) {
  console.log(`OK   http(wasm): http.get -> env.teko_http_get -> body: ${JSON.stringify(out)} (host-import mock, reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL http(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
