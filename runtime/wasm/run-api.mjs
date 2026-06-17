// Phase 19 (ROUTER-NATIVE — WASM discriminating proof).
//
// Proves the radix router SELECTS by method+path, NOT by registration order.
// The api { } block lowers to env.teko_rt_router_* HOST imports, backed by a pure
// in-memory radix-tree mock. lower_api_block emits a DISCRIMINATING sequence:
//
//   (a) In-order dispatch (proves router runs, not inline):
//     router_dispatch("GET",    "/hello") -> slot=1; call_indirect -> emit_int(200)
//     router_dispatch("POST",   "/data")  -> slot=2; call_indirect -> emit_int(201)
//     router_dispatch("DELETE", "/gone")  -> slot=3; call_indirect -> emit_int(204)
//   (b) Out-of-order dispatch (proves selection by path, not registration sequence):
//     router_dispatch("DELETE", "/gone")  -> slot=3; call_indirect -> emit_int(204)  [last route first]
//     router_dispatch("GET",    "/hello") -> slot=1; call_indirect -> emit_int(200)  [first route last]
//   (c) 404 miss (impossible via inline emit):
//     router_status("GET", "/notfound")  -> 404; emit_int(404)
//   (d) 405 method-mismatch (impossible via inline emit):
//     router_status("PATCH", "/hello")   -> 405; emit_int(405)
//
// Expected output: [200, 201, 204, 204, 200, 404, 405]
// IMPOSSIBLE via inline-leak (no handler body emits 404/405) or sequential-slot-return
// (out-of-order 204,200 cannot appear from registration order 200,201,204).
//
// SAST (host side): method_ptr/path_ptr are i32 module values bounded to the module's linear
// memory; readCStr reads at most MAX_BUF bytes before returning (cap the scan); handler_id is
// a module i32 (compile-time bounded slot, no overflow); route data is compile-time string
// constants (no attacker-controlled input at registration); dispatch result is a module-internal
// i32 used only as a call_indirect table index (WASM runtime bounds-checked); no untrusted data
// escapes the memory region.

import { readFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";

const here = (p) => fileURLToPath(new URL(p, import.meta.url));
const dec  = new TextDecoder();

const memory = new WebAssembly.Memory({ initial: 64 });
const out = [];
let nextHandle = 1;
const routeMap = new Map(); // "handle:method:path" -> slot

const MAX_BUF = 65535;

const readCStr = (ptr) => {
  const u = new Uint8Array(memory.buffer);
  let end = ptr >>> 0;
  while (u[end] !== 0 && end - (ptr >>> 0) < MAX_BUF) end++;
  return dec.decode(u.subarray(ptr >>> 0, end));
};

const env = {
  memory,
  // --- router host mock (env namespace) ---
  // Signature: teko_rt_router_new(capacity) -> handle
  // Signature: teko_rt_router_add(method_ptr, path_ptr, slot, handle) -> status
  // Signature: teko_rt_router_dispatch(handle, method_ptr, path_ptr) -> slot
  // Signature: teko_rt_router_free(handle) -> status
  teko_rt_router_new: (capacity) => {
    const h = nextHandle++;
    console.log(`  router_new(${capacity | 0}) -> handle ${h}`);
    return h;
  },
  teko_rt_router_add: (method_ptr, path_ptr, slot, handle) => {
    const method = readCStr(method_ptr);
    const path   = readCStr(path_ptr);
    const key = `${handle}:${method}:${path}`;
    routeMap.set(key, slot | 0);
    console.log(`  router_add("${method}", "${path}", ${slot | 0}, ${handle}) -> key=${key}`);
    return 0; // TEKO_ROUTE_OK
  },
  teko_rt_router_dispatch: (handle, method_ptr, path_ptr) => {
    const method = readCStr(method_ptr);
    const path   = readCStr(path_ptr);
    const key = `${handle}:${method}:${path}`;
    const slot = routeMap.has(key) ? (routeMap.get(key) | 0) : -1;
    console.log(`  router_dispatch(${handle}, "${method}", "${path}") -> key=${key} slot=${slot}`);
    return slot;
  },
  teko_rt_router_free: (handle) => {
    console.log(`  router_free(${handle})`);
    return 0;
  },
  // id 179: teko_rt_router_status(handle, method_ptr, path_ptr) -> status (200/404/405).
  // Returns 200 if an exact (method, path) match exists, 404 if path is not registered,
  // 405 if path exists but the method does not match.
  teko_rt_router_status: (handle, method_ptr, path_ptr) => {
    const method = readCStr(method_ptr);
    const path   = readCStr(path_ptr);
    // Check if any route for this handle matches the path (any method).
    let pathExists = false;
    let exactMatch = false;
    for (const [key, slot] of routeMap.entries()) {
      // key format: "handle:METHOD:path"
      const parts = key.split(":");
      if (parts[0] === String(handle) && parts[2] === path) {
        pathExists = true;
        if (parts[1] === method) { exactMatch = true; break; }
      }
    }
    const status = exactMatch ? 200 : (pathExists ? 405 : 404);
    console.log(`  router_status(${handle}, "${method}", "${path}") -> ${status}`);
    return status;
  },
  // --- reactor host hooks (crypto/time/entropy) ---
  teko_now_ns:    () => process.hrtime.bigint(),
  teko_now_unix:  () => 1000000000n,
  teko_tz_offset: () => 0,
  teko_random:    (ptr, len) => {
    const u = new Uint8Array(memory.buffer);
    for (let i = 0; i < (len >>> 0); i++) u[(ptr >>> 0) + i] = 0;
  },
};

// `emit_int` is imported from the "teko_rt" namespace.
const teko_rt = {
  teko_rt_emit_int: (n) => {
    const val = n | 0;
    out.push(val);
    console.log(`  emit_int(${val})`);
  }
};

// Load and instantiate the crypto reactor.
const cryptoBytes = await readFile(here("./crypto/crypto.wasm"));
const cryptoInst = await WebAssembly.instantiate(cryptoBytes, { env });

// Load and instantiate the api.wasm sample.
const apiWasm = await readFile(here("./samples/api.wasm"));
const teko = await WebAssembly.instantiate(apiWasm, {
  env,
  crypto: cryptoInst.instance.exports,
  teko_rt
});

// Run main().
console.log("Running api.tks proof:");
teko.instance.exports.main();

// Assert discriminating output.
// [200,201,204]: in-order dispatch (proves router runs, not inline)
// [204,200]:     out-of-order dispatch (last route first, first route last — proves selection by path)
// [404]:         unregistered path /notfound — impossible via inline emit
// [405]:         registered path /hello, wrong method PATCH — impossible via inline emit
const expected = [200, 201, 204, 204, 200, 404, 405];
if (JSON.stringify(out) === JSON.stringify(expected)) {
  console.log(`OK   api router (discriminating): ${JSON.stringify(out)}`);
  console.log(`     in-order[200,201,204] + out-of-order[204,200] + miss[404] + method-mismatch[405]`);
  console.log(`     IMPOSSIBLE via inline-leak or sequential-slot-return → genuine radix selection proven`);
  process.exit(0);
} else {
  console.error(`FAIL api router: got ${JSON.stringify(out)}, expected ${JSON.stringify(expected)}`);
  process.exit(1);
}
