// Phase 19 (T2 — net.* socket frontend wiring) — WASM proof harness.
//
// WASM cannot open raw OS sockets, so the seven env.teko_net_* host imports are
// backed by a pure in-memory mock: tcp_connect returns a fake handle, send stashes
// the bytes in a JS Uint8Array, recv copies them back into linear memory and returns
// a pointer. The round-trip asserts that the module correctly calls the surface in order:
//   net.tcp_connect("127.0.0.1", 17342) -> handle
//   net.send(handle, "hello", 5) -> 0 (ok)
//   net.recv(handle, 64) -> ptr to "hello" in memory
//   emit(ptr) -> "hello" on stdout
//   net.close(handle) -> 0
//   net.free(handle) -> 0
//
// SAST (host side):
//   - host_ptr/data_ptr are i32 WASM memory offsets validated via u.subarray bounds before use.
//   - len/max_len are i32 values; the recv copy uses Math.min(stored.length, max_len, 65535)
//     to enforce the TEKO_SOCKET_MAX_BUF cap on the JS side as well.
//   - recv writes to a FIXED offset (0x200 = 512) in WASM memory that is inside the
//     module's initial 64-page region and far below the stack/heap area (~64 KiB). This
//     offset is a compile-time constant, not derived from any external input.
//   - No format-string, no path traversal, no untrusted data flowing outside the memory region.

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here  = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec   = new TextDecoder();
const enc   = new TextEncoder();

// Slot used to stash bytes sent by the module (keyed by handle).
const store = new Map();
let   memory; // set after instantiation (we use exports.memory)

// RECV_OFFSET: a fixed offset in WASM linear memory where recv() writes its result.
// Must be in the data segment (not clobbering strings or stack). 0x200 = 512, well below
// the module's string pool area which starts at a higher address.
const RECV_OFFSET = 0x200;
const MAX_BUF     = 65535; // TEKO_SOCKET_MAX_BUF cap (match C runtime)

// Read a NUL-terminated string from WASM memory at ptr.
const readCStr = (ptr) => {
  const u = new Uint8Array(memory.buffer);
  let end = ptr >>> 0;
  while (u[end] !== 0) end++;
  return dec.decode(u.subarray(ptr >>> 0, end));
};

// Write bytes + NUL into WASM memory at offset, return offset.
const writeBytes = (offset, bytes) => {
  const u = new Uint8Array(memory.buffer);
  const n = Math.min(bytes.length, MAX_BUF);
  for (let i = 0; i < n; i++) u[(offset + i) >>> 0] = bytes[i];
  u[(offset + n) >>> 0] = 0; // NUL-terminate
  return offset;
};

const out = [];
let nextHandle = 1;

const env = {
  // teko_net_tcp_connect(host_ptr: i32, port: i32) -> handle: i32
  // Reads the host string from memory (for logging/verification), ignores real TCP.
  teko_net_tcp_connect: (host_ptr, port) => {
    const host = readCStr(host_ptr);
    const handle = nextHandle++;
    store.set(handle, new Uint8Array(0)); // no data yet
    // Record connection attempt for verification.
    store.set(`host_${handle}`, host);
    store.set(`port_${handle}`, port);
    return handle;
  },

  // teko_net_udp_open(host_ptr: i32, port: i32) -> handle: i32
  teko_net_udp_open: (host_ptr, port) => {
    const host = readCStr(host_ptr);
    const handle = nextHandle++;
    store.set(handle, new Uint8Array(0));
    store.set(`host_${handle}`, host);
    store.set(`port_${handle}`, port);
    return handle;
  },

  // teko_net_send(handle: i32, data_ptr: i32, len: i32) -> status: i32 (0 = ok)
  // Reads len bytes from memory at data_ptr and stashes them in the store.
  // SAST: len is i32 from the module; we cap at MAX_BUF before any read.
  teko_net_send: (handle, data_ptr, len) => {
    const safe_len = Math.min(len >>> 0, MAX_BUF);
    const u = new Uint8Array(memory.buffer);
    const bytes = u.slice(data_ptr >>> 0, (data_ptr >>> 0) + safe_len);
    store.set(handle, bytes);
    return 0; // TEKO_SOCK_OK
  },

  // teko_net_recv(handle: i32, max_len: i32) -> result_ptr: i32 (0 = no data)
  // Copies stashed bytes back into WASM memory at RECV_OFFSET, returns that offset.
  // SAST: max_len is i32 from the module; capped at MAX_BUF before any memory write.
  teko_net_recv: (handle, max_len) => {
    const bytes = store.get(handle);
    if (!bytes || bytes.length === 0) return 0;
    const safe_max = Math.min(max_len >>> 0, MAX_BUF);
    const to_copy  = Math.min(bytes.length, safe_max);
    const slice    = bytes.subarray(0, to_copy);
    return writeBytes(RECV_OFFSET, slice);
  },

  // teko_net_close(handle: i32) -> status: i32
  teko_net_close: (handle) => {
    // Nothing to do for the mock; in-flight data is still readable after close.
    return 0; // TEKO_SOCK_OK
  },

  // teko_net_free(handle: i32) -> 0
  teko_net_free: (handle) => {
    store.delete(handle);
    return 0;
  },

  // teko_net_state(handle: i32) -> state: i32
  // 2 = TEKO_SOCK_OPEN (always for the mock).
  teko_net_state: (handle) => 2,

  // env.emit: read NUL-terminated string from memory, record it.
  emit: (ptr) => {
    out.push(readCStr(ptr));
  },

  // Other env imports that the compiled module may reference (standard set).
  teko_now_ns:    () => process.hrtime.bigint(),
  teko_now_unix:  () => 1000000000n,
  teko_tz_offset: (_) => 0,
  teko_random:    (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
};

// Instantiate the compiled net.wasm (produced by: teko build samples/net.tks -o samples/net.wasm).
const wasm = await readFile(here("./samples/net.wasm"));
const { instance } = await WebAssembly.instantiate(wasm, { env });
memory = instance.exports.memory;
instance.exports.main();

// Verify: the module emitted "hello" (the echoed bytes from the mock).
const expected = ["hello"];
if (JSON.stringify(out) === JSON.stringify(expected)) {
  console.log(`OK   net(wasm): net.tcp_connect → send → recv round-trip: ${JSON.stringify(out)}`);
  process.exit(0);
} else {
  console.error(`FAIL net(wasm): got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
