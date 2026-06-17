// Phase 19 (T2 — net.* socket frontend wiring) — WASM proof harness.
//
// WASM cannot open raw OS sockets, so the seven net.* ops lower to env.teko_net_*
// HOST imports, backed here by a pure in-memory mock (no real TCP). The emitted module
// has the SAME shape as every other Teko WASM sample: it imports the compiled-C reactor
// (crypto.wasm) and shares ONE linear memory with it (env.memory), and it imports `emit`
// from the "teko_rt" namespace. So this host mirrors run-class.mjs: create the shared
// memory, instantiate the reactor against it, then instantiate the sample with
// { env, crypto: reactor.exports, teko_rt }. The round-trip asserts the module drives the
// surface in order:
//   net.tcp_connect("127.0.0.1", 17342) -> handle
//   net.send(handle, "hello", 5)        -> 0 (ok)   [stashed by the mock]
//   net.recv(handle, 64)                -> ptr to "hello" in linear memory
//   emit(ptr)                           -> "hello"
//   net.close(handle) / net.free(handle)
//
// SAST (host side): data_ptr/len/max_len are i32 module values; send caps the read and
// recv caps the write at MAX_BUF (TEKO_SOCKET_MAX_BUF) before touching memory; recv writes
// to a fixed compile-time offset; no untrusted data leaves the memory region; no
// format-string / path traversal.

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec  = new TextDecoder();

const memory = new WebAssembly.Memory({ initial: 64 });
const store  = new Map();   // handle -> Uint8Array stashed by send
let   nextHandle = 1;
const out = [];

const MAX_BUF     = 65535;   // TEKO_SOCKET_MAX_BUF cap (match the C runtime)
const RECV_OFFSET = 0x200;   // fixed scratch offset where recv writes its result

const readCStr = (ptr) => {
  const u = new Uint8Array(memory.buffer);
  let end = ptr >>> 0;
  while (u[end] !== 0) end++;
  return dec.decode(u.subarray(ptr >>> 0, end));
};
const writeBytes = (offset, bytes) => {
  const u = new Uint8Array(memory.buffer);
  const n = Math.min(bytes.length, MAX_BUF);
  for (let i = 0; i < n; i++) u[(offset + i) >>> 0] = bytes[i];
  u[(offset + n) >>> 0] = 0; // NUL-terminate
  return offset;
};

const env = {
  memory,
  // --- net.* host mock (env namespace) -------------------------------------------------
  teko_net_tcp_connect: (host_ptr, port) => {
    const h = nextHandle++;
    store.set(h, new Uint8Array(0));
    store.set(`host_${h}`, readCStr(host_ptr));
    store.set(`port_${h}`, port | 0);
    return h;
  },
  teko_net_udp_open: (host_ptr, port) => {
    const h = nextHandle++;
    store.set(h, new Uint8Array(0));
    return h;
  },
  teko_net_send: (handle, data_ptr, len) => {
    const safe_len = Math.min(len >>> 0, MAX_BUF);
    const u = new Uint8Array(memory.buffer);
    store.set(handle, u.slice(data_ptr >>> 0, (data_ptr >>> 0) + safe_len));
    return 0; // TEKO_SOCK_OK
  },
  teko_net_recv: (handle, max_len) => {
    const bytes = store.get(handle);
    if (!bytes || bytes.length === 0) return 0;
    const to_copy = Math.min(bytes.length, max_len >>> 0, MAX_BUF);
    return writeBytes(RECV_OFFSET, bytes.subarray(0, to_copy));
  },
  teko_net_close: (handle) => 0,
  teko_net_free:  (handle) => { store.delete(handle); return 0; },
  teko_net_state: (handle) => 2, // TEKO_SOCK_OPEN
  // --- reactor host hooks (crypto/time/entropy) — net sample uses none, stub them ------
  teko_now_ns:    () => process.hrtime.bigint(),
  teko_now_unix:  () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random:    (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
};

// `emit` is imported from the "teko_rt" namespace (extern fn emit from "teko_rt").
const teko_rt = { teko_rt_emit: (ptr) => { out.push(readCStr(ptr)); } };

const reactor = await WebAssembly.instantiate(await readFile(here("./crypto/crypto.wasm")), { env });
const sample  = await readFile(here("./samples/net.wasm"));
const teko    = await WebAssembly.instantiate(sample, { env, crypto: reactor.instance.exports, teko_rt });
teko.instance.exports.main();

const expected = ["hello"];
if (JSON.stringify(out) === JSON.stringify(expected)) {
  console.log(`OK   net(wasm): net.tcp_connect -> send -> recv round-trip: ${JSON.stringify(out)} (host-import client, reactor-backed)`);
  process.exit(0);
} else {
  console.error(`FAIL net(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
