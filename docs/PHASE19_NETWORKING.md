# Phase 19 — Native Networking & Web Architecture (NativeAOT/FFI posture)

> **Status:** IN PROGRESS — branch `feat/phase-19-networking` (principal Draft PR #13 → `main`).
> Planned/managed under the **Orchestration Doctrine** (`docs/ORCHESTRATION_DOCTRINE.md`): the Master
> Agent (Opus) decomposes into breadcrumbs and delegates to Tech Lead (Sonnet) / Developer (Haiku)
> subagents; each breadcrumb ships as a sub-PR **into this phase branch** (PM merges after review +
> SAST + CI); the **owner (PO) merges the principal PR into `main`** at the end.

References — never duplicates — `docs/plan.md` §Phase 19, `CLAUDE.md` (bars), and the parallel
TLS-track doc `docs/PHASE19_TLS_TRACK.md`.

---

## 0. Owner-locked architecture: the C#/.NET NativeAOT posture

Phase 19 produces a **ready-to-run native binary that dynamically links what each OS already
provides**, **FFI**s to **audited system libraries** for heavy/security-critical capabilities, and
writes **native code ONLY for teko's value-add**. This is the decisive simplification: we are **not**
building a fully-static, dependency-free stack in Phase 19.

- **A1** pragmatic MVP · **C1** cooperative async (Phase-14 scheduler; a routine blocked on
  `accept`/`recv` yields). Real async backends (io_uring/kqueue/IOCP) remain a later perf track.
- **B — target split:** **NATIVE = full SERVER + CLIENT** at every layer (TCP/UDP → HTTP → WebSocket
  → router); `bind`/`listen`/`accept` **and** `connect`/`send`/`recv`/`close` are **required** on
  native. **WASM = client-first** (host-import `fetch`/`ws`); **server/listen surfaces are still
  emitted for WASM** but run over **WASI `wasi-sockets`** where the runtime supports it and
  **fall back to host-import client in the browser**, documented explicitly (§1.2).
- **One teko surface per capability, per-OS backend** — the **same pattern as the real
  monotonic/wall clock** (`clock_gettime` / `QueryPerformanceCounter` behind one surface). Now
  `tls.*` and the heavy `compress.*` codecs sit on **OpenSSL / SChannel / Secure Transport / zlib /
  brotli / …** behind one teko surface each.
- **Dynamic-link + FFI on all native targets** (no static constraint) — removes the biggest source
  of complexity.

### 0.1 Native matrix = exactly what CI already covers
Linux **glibc** (x86_64 / arm64 / riscv64), **macOS** Intel + Apple Silicon (Darwin), **Windows**
(x86_64 / arm64). Other Unix/BSD only via POSIX **where it comes for free**. No new platforms.

### 0.2 What is NATIVE (teko's value-add) vs FFI (audited system libs)
| Native in teko (we write it) | FFI to system/audited lib (we bind it) |
|---|---|
| Sockets — BSD sockets + Winsock (client **and** native server) | **TLS** — OpenSSL / SChannel / Secure Transport (`tls.*`) |
| HTTP/1.1 request/response codec | **brotli / zstd / lzma** heavy compressors (`compress.*`) |
| WebSocket framing (RFC 6455) | **QUIC / HTTP-3** — quiche / msquic |
| The router (AOT static radix tree + middleware) | **HTTP/2** — nghttp2 (gRPC transport) |
| **Small native DEFLATE/gzip/zlib core** = teko's optional `native` backend (§0.3) | deflate default fallback = **bundled zlib-ng**; `system` zlib if probed |
| Protobuf wire codec (small, no system dep) | |
| **FFI-CORE** marshalling/lifetime/error-mapping layer (the SAST-critical boundary) | |

**Crypto:** Phase-13 primitives are **kept as-is, unchanged** (they are not TLS). In Phase 19,
**TLS/crypto for the network stack is FFI-ONLY** — we do **not** roll our own TLS. (The from-scratch
native TLS 1.3/1.2 + the crypto-for-TLS only existed to justify a static/musl mode — now a Future
target, §5.)

### 0.3 FFI-CORE dependency model (LOCKED)
Every FFI capability resolves through a **declared backend hierarchy** (highest preference first),
**lazily by use**, recorded deterministically in a lockfile. This is the contract FFI-CORE implements.

- **Backend hierarchy per capability** (declared in the manifest):
  1. **`system`** — the developer OS's library via FFI, found by a **build-time probe** (e.g. system
     OpenSSL, system libzstd). Zero shipped dependency when present.
  2. **`bundled`** — a **teko-DESIGNATED DEFAULT/standard library, pinned + vendored** (reproducible).
     This is the **primary fallback** — **NOT** a from-scratch native impl. Designated defaults:
     **TLS = BoringSSL/OpenSSL (vendored)**, **deflate = zlib-ng**, **brotli = brotli reference**,
     **zstd = libzstd**, **lzma = xz/liblzma**.
  3. **`native`** — teko's **own** implementation, **OPTIONAL**, only where it exists today:
     **DEFLATE/gzip now**. Never the default fallback; an explicit opt-in where present.
- **Lazy by use** — a library (system or bundled) is **required/linked ONLY if a used surface** (in
  code or a transitive dep) needs it. A declared-but-unused dependency is a **warning, not an error**.
  A program that never touches `tls.*` links **no** TLS library into the binary.
- **Manifest is the source of truth** — **no silent auto-detection**. Resolution is deterministic and
  recorded in a **lockfile (`.tkp.lock`)** (which backend + version won per capability).
- **Build-time assertion, FAIL-LOUD** — if **nothing** in a capability's chain resolves, the build
  **fails with an actionable message**: which library, how to install it per-OS, or how to switch to
  `bundled`/`native`. **Never a silent stub.**
- **WASM is orthogonal** — WASM always uses the host-import / WASI backend; the FFI backend hierarchy
  applies to **native only**.

### 0.4 Manifest unification (`.tkp` ≡ the Phase-21 `.teko_meta`) — LOCKED
There is **ONE** manifest format: **`.tkp` is unified with the planned `.teko_meta` (Phase 21)** — we
do **not** invent two formats. **Phase 19 defines ONLY the FFI/deps section** (the backend hierarchy +
fallback chain + lazy-by-use, §0.3) and the `.tkp.lock` lockfile; the **full manifest schema is
refined in Phase 21**. FFI-CORE owns the FFI/deps section + the resolver + the lockfile writer.

---

## 1. Token / surface semantics (server vs client; how each is proven on BOTH targets)

The "no dead tokens" bar binds the reserved lexer tokens
`api middleware get post put delete patch head options rpc websocket` (+ bandwidth units
`kbps`/`mbps`/`gbps`). The reserved **web router tokens are SERVER route-definition constructs**;
client HTTP/WS/RPC usage rides **dotted-identifier surfaces** (not tokens). Resolution per surface:

| Token / surface | Kind | Backend | Native | WASM | Proof |
|---|---|---|---|---|---|
| `api`, `middleware`, `get/post/put/delete/patch/head/options` | **SERVER route-definition tokens** | native code | **real listening server** (`bind/listen/accept`) + AOT static **radix-tree** dispatch; `middleware` = server interceptor chain | WASI server where supported; **browser: no listen** → synthetic | **Target-agnostic routing:** build the radix tree + dispatch a **SYNTHETIC in-module request** (no socket) on BOTH targets; real `bind/listen/accept` native (+ WASI), honestly marked |
| `websocket` | **SERVER WS endpoint** token | native code | real server WS endpoint (accept + RFC 6455) | WASI / synthetic | same target-agnostic proof |
| `rpc` | RPC | native code | server **and** client (length-prefixed) | client (host-import) + synthetic server route | native `.tks`; WASM client `.mjs` + synthetic |
| `http.*` | **client** dotted surface | native code | real client over socket | host-import `fetch` | live on BOTH |
| `net.*` (`tcp_connect/…` + native `tcp_listen/accept`) | dotted surface | native code | client **and** server primitives | WASM: connect/send/recv (+ WASI listen); browser listen absent, marked | native `.tks`; WASM `.mjs` |
| `ws.*` (client WebSocket) | **client** dotted surface | native code | real client | host-import client | live on BOTH |
| `tls.*` (`tls.client`/native `tls.server`) | dotted surface | **FFI** (system TLS) | OpenSSL/SChannel/Secure Transport | host-import TLS (Node TLS); WASI-TLS where present | native `.tks` over FFI; WASM host-import |
| `compress.deflate/gzip/zlib` | dotted surface | **native DEFLATE core** | in-process | in-reactor | KAT vs RFC/zlib vectors, both targets |
| `compress.brotli/zstd/lzma` | dotted surface | **FFI** (system lib) | libbrotli/libzstd/liblzma | host-import where available (Node has brotli) | native `.tks` over FFI; WASM host where present, else documented gap |
| `kbps/mbps/gbps` | throttle | native | `limit(10mbps)` | same | normalized bps literal |

`net`/`http`/`tls`/`compress`/`ws` are **dotted surfaces, not tokens** — QUIC/HTTP-2/3/4/full-gRPC
sequence as later FFI tracks with **no dead tokens**.

### 1.1 Why the synthetic-request proof is legitimate (not a stubbed token)
The server tokens emit two separable things: (a) the **AOT radix tree + middleware chain + handler
dispatch** (target-agnostic logic) and (b) the **socket accept loop** (native/WASI syscalls). The
WASM proof exercises (a) end-to-end with a real in-module request value and asserts the correct
handler/middleware run and the right response — the token is **fully lowered and executed**; only the
OS accept syscall (b) is native/WASI-only.

### 1.2 WASM server posture (honest)
WASM **server/listen surfaces are emitted** so the grammar/emission is identical across targets, and
run for real over **WASI `wasi-sockets`** where the host supports it. In a **browser** there are no
listening sockets — `listen` there **fails/falls back to the host-import client**, and this is
**documented explicitly** at the surface (not silently stubbed).

---

## 2. Parallelization DAG (FFI posture — fewer, thinner tracks)

Native codecs/runtimes are pure + KAT-testable with zero socket dependency; FFI tracks are thin
bindings gated by **FFI-CORE**. Integration funnels through **T2** (socket wiring).

### Legend: ⟂ independent (now) · → depends on · ⏸ deferred (§5)

### Wave 0 — start NOW, fully parallel (separate files)
**Native value-add:**
- **T1a — socket CLIENT** `teko_socket.c` (`tcp_connect`/`udp_connect`/`send`/`recv`/`close`; POSIX +
  Winsock; calloc handle table, fail-loud bounds). ⟂
- **T1b — socket SERVER (native)** `teko_socket_server.c` (`bind`/`listen`/`accept` loop;
  scheduler-integrated; native + WASI-gated). ⟂
- **HTTP-CODEC** — request/response build+parse (headers/body/chunked), KAT'd. ⟂
- **WS-CODEC** — RFC 6455 frame encode/decode/mask/UTF-8, KAT'd. ⟂
- **ROUTER-CORE** — AOT static radix-tree builder + middleware chain + handler dispatch over a
  **synthetic request** (no socket), KAT'd; target-agnostic — proves the server tokens on WASM. ⟂
- **DEFLATE-CORE** — small native DEFLATE (RFC 1951) + gzip (RFC 1952) + zlib (RFC 1950) +
  CRC32/Adler-32, KAT'd vs zlib vectors. ⟂
- **PROTOBUF-CODEC** — protobuf wire encode/decode (for gRPC), KAT'd. ⟂

**FFI foundation:**
- **FFI-CORE** — the marshalling / lifetime / error-mapping layer + per-OS library resolution
  (link-time or `dlopen`/`LoadLibrary`) for calling audited system libs safely. The SAST-critical
  enabler for every FFI track. ⟂

### Wave 1 — after **T1a/T1b → T2** (+ FFI-CORE for the FFI tracks); parallel
- **T2 — socket wiring**: CMake + `teko_rt_socket_*` (client) + `teko_rt_server_*` (native/WASI) +
  reactor SRCS/EXPORTS (client subset) + frontend `net.*` + native/WASM emit + proofs. → T1a,T1b
- **HTTP-INT (client)** `http.*` over socket. → T2 + HTTP-CODEC
- **HTTP-SRV (server)** accept → parse → router → response → send (native + WASI). → T2 + HTTP-CODEC + ROUTER-CORE
- **WS-INT (client)** `ws.*` connect/send/recv. → T2 + HTTP-CODEC + WS-CODEC
- **RPC** native server+client + WASM client. → T2
- **UNITS** `limit(10mbps)` throttle. → T2
- **COMPRESS-INT** Content-Encoding/Transfer-Encoding (DEFLATE core) + permessage-deflate. → HTTP/WS-INT + DEFLATE-CORE
- **TLS-FFI** `tls.client` (+ native `tls.server`) via system TLS. → FFI-CORE  *(FFI track)*
- **COMPRESS-FFI** `compress.brotli/zstd/lzma` via system libs. → FFI-CORE + DEFLATE-CORE surface  *(FFI track)*

### Wave 2 — after Wave-1 deps; parallel
- **ROUTER-NATIVE** — `api`/`middleware`/verbs as a real server (WASM = synthetic/WASI). → HTTP-SRV/ROUTER-CORE
- **WS-SRV** — `websocket` server endpoint (WASM = synthetic/WASI). → HTTP-SRV + WS-CODEC
- **TLS-INT** — `https` = HTTP over TLS-FFI. → TLS-FFI + HTTP-INT/SRV
- **H2-FFI** — HTTP/2 via nghttp2. → FFI-CORE + TLS-FFI  *(FFI track)*
- **QUIC-FFI** — QUIC via quiche/msquic. → FFI-CORE + TLS-FFI  *(FFI track)*

### Wave 3 — after Wave-2; parallel
- **GRPC** — gRPC over H2-FFI + PROTOBUF-CODEC. *(minimal `rpc` already live in Wave 1)*
- **H3-FFI** — HTTP/3 over QUIC-FFI.

### Capstone
- **CAPSTONE** — NATIVE: a routed `api` server + a `websocket` endpoint + an outbound `http.get`
  client + a `compress.gzip` round-trip + a `tls.client` FFI call, one program. WASM: the client
  calls + the synthetic-request routing proof. Both green. → Wave 0–2.

### 2.1 Concurrency hygiene
Shared files: `CMakeLists.txt`, `build-crypto-reactor.sh` (`SRCS`/`EXPORTS` — client subset; native
server + FFI TUs excluded, `#if !defined(__wasm__)` guarded), and the runtime-id→symbol table.
Pre-allocated `OP_CALL_RUNTIME` id ranges: net-client 60–69 · net-server 70–79 · http 80–99 ·
ws 100–109 · rpc 110–119 · deflate-core 120–139 · compress-ffi(brotli/zstd/lzma) 140–169 ·
router/throttle 175–179 · tls-ffi 180–199 · h2-ffi 220–239 · quic-ffi 240–259 · h3-ffi 260–279 ·
grpc/protobuf 280–299. Append-only edits at distinct anchors; FFI bindings go through FFI-CORE, not
ad-hoc `extern`.

---

## 3. Breadcrumb table (medium → Tech Lead; small → Developer)

| Track | Wave | Kind | Role | Native | WASM | Makes live | Deps |
|---|---|---|---|---|---|---|---|
| T1a socket client | 0 | native | Tech Lead | ✓ | ✓ | enabler | — |
| T1b socket server | 0 | native | Tech Lead | ✓ | WASI/synthetic | enabler | — |
| HTTP-CODEC | 0 | native | Tech Lead | ✓ | ✓ | (codec) | — |
| WS-CODEC | 0 | native | Developer | ✓ | ✓ | (codec) | — |
| ROUTER-CORE | 0 | native | Tech Lead | ✓ | ✓ | (router engine) | — |
| DEFLATE-CORE | 0 | native | Tech Lead | ✓ | ✓ | `compress.deflate/gzip/zlib` | — |
| PROTOBUF-CODEC | 0 | native | Developer | ✓ | ✓ | (codec) | — |
| FFI-CORE | 0 | FFI infra | Tech Lead | ✓ | n/a (host-import) | (FFI boundary) | — |
| T2 socket wiring | 1 | native | Tech Lead | c+s | client+WASI | `net.*` | T1a,T1b |
| HTTP-INT (client) | 1 | native | Tech Lead | ✓ | ✓ | `http.*` | T2,HTTP-CODEC |
| HTTP-SRV (server) | 1 | native | Tech Lead | ✓ | WASI/synthetic | (server engine) | T2,HTTP-CODEC,ROUTER-CORE |
| WS-INT (client) | 1 | native | Tech Lead | ✓ | ✓ | `ws.*` | T2,HTTP-CODEC,WS-CODEC |
| RPC | 1 | native | Developer | s+c | client | `rpc` | T2 |
| UNITS throttle | 1 | native | Developer | ✓ | ✓ | `kbps/mbps/gbps` | T2 |
| COMPRESS-INT | 1 | native | Tech Lead | ✓ | ✓ | Content-Encoding/permessage-deflate | HTTP/WS-INT,DEFLATE-CORE |
| **TLS-FFI** | 1 | **FFI** | Tech Lead | ✓ | host-import | `tls.*` | FFI-CORE |
| **COMPRESS-FFI** | 1 | **FFI** | Tech Lead | ✓ | host where avail | `compress.brotli/zstd/lzma` | FFI-CORE,DEFLATE-CORE |
| ROUTER-NATIVE | 2 | native | Tech Lead | real server | synthetic/WASI | `api middleware get post put delete patch head options` | HTTP-SRV/ROUTER-CORE |
| WS-SRV | 2 | native | Tech Lead | real server | synthetic/WASI | `websocket` | HTTP-SRV,WS-CODEC |
| TLS-INT | 2 | FFI | Tech Lead | ✓ | host | `https` | TLS-FFI |
| **H2-FFI** | 2 | **FFI** | Tech Lead | ✓ | client | `http2.*` | FFI-CORE,TLS-FFI |
| **QUIC-FFI** | 2 | **FFI** | Tech Lead | ✓ | client | `quic.*` | FFI-CORE,TLS-FFI |
| GRPC | 3 | FFI+native | Tech Lead | ✓ | client | `grpc.*` | H2-FFI,PROTOBUF |
| H3-FFI | 3 | FFI | Tech Lead | ✓ | client | `http3.*` | QUIC-FFI |
| CAPSTONE | — | both | Master | ✓ | ✓ | — | 0–2 |

**Token-completeness milestone** (every reserved web token + units live: native real server/client +
WASM client/synthetic) = **ROUTER-NATIVE + WS-SRV (Wave 2) + RPC/UNITS (Wave 1)** — reachable with
**only native tracks**; needs no FFI (TLS/compress-heavy/QUIC/H2 are dotted surfaces).

---

## 4. Compression tiering (FFI posture + the §0.3 backend hierarchy)
- **`compress.deflate/gzip/zlib`** — resolves `system` zlib → **bundled zlib-ng** (default fallback)
  → **`native` teko DEFLATE core** (the optional native backend that exists today). The native core
  (DEFLATE RFC 1951 + gzip RFC 1952 + zlib RFC 1950 + CRC32/Adler-32) is **also what compiles into
  the WASM reactor** (no FFI on WASM), so DEFLATE-CORE remains a Wave-0 native track. Drives
  Content-Encoding/Transfer-Encoding + permessage-deflate.
- **`compress.brotli/zstd/lzma`** — resolve `system` → **bundled** (brotli reference / libzstd /
  liblzma) via FFI-CORE; **no `native` backend**. On WASM, host-import where available (Node has
  brotli), else a documented gap. `compress.*` is a **free dotted surface** (verified: no reserved
  compression token; `bundle`/`minify` are Phase-20 asset tooling).

### SAST / security
Compression + encryption → **CRIME/BREACH** oracle risk: compression is a **separate, opt-in layer**,
**never** auto-applied to encrypted/secret payloads; TLS carries **no record compression**. This is
**safe composition only — Phase-13 crypto primitives are unchanged.** Decompression (native DEFLATE +
any FFI lib output) gets **output size caps** (decompression-bomb guard), bounds-checked window
access, overflow-safe length math.

---

## 5. Future targets (explicitly deferred — out of Phase 19 scope)
- **musl / 100% static, dependency-free binary** — the static-link target. Dropped from Phase 19;
  documented future work.
- **From-scratch native TLS 1.3/1.2 + crypto-for-TLS** (RFC 8448 stack) — only needed for the
  static/musl mode; deferred with it. (Phase-13 crypto primitives stay as-is, unchanged.)
- **HTTP/4** — no published standard / RFC exists. Hard defer (spec gap).
- **Async io_uring/kqueue/IOCP backends** — perf-only; behind the same `net.*` surface; no new token.
- **WASM browser listening server** — platform impossibility; honestly marked (browser falls back to
  host-import client; WASI server is supported where the runtime provides `wasi-sockets`).

---

## 6. Size estimate (FFI posture — well below the prior ~26–37k)
Replacing from-scratch TLS (~3–4.5k), from-scratch brotli/zstd/lzma (~6–9k), and from-scratch QUIC
(large) with **thin FFI bindings (~300–800 LOC each incl. marshalling + KAT/proof)** collapses the
estimate:

- **A1 token-completeness core** (T1a/T1b/T2, HTTP codec+int+srv, WS codec+int+srv, router core+
  native, RPC, UNITS, DEFLATE core, FFI-CORE, protobuf): **~8,000–11,000 LOC**.
- **FFI binding tracks** (TLS, brotli, zstd, lzma, H2/nghttp2, QUIC/quiche, gRPC, H3): **~3,000–5,000
  LOC** total (thin bindings + proofs).
- **Full Phase-19 ambition under the FFI posture:** **~11,000–16,000 LOC** — roughly **half** the
  prior from-scratch estimate, and most of it the native value-add (sockets/HTTP1/WS/router/DEFLATE).

---

## 7. Bars (unchanged) + per-breadcrumb SAST gate
One increment/commit; ASan+UBSan **both** dispatch paths + **TSan** (the FFI boundary is exactly
where the prior wild-free hid — see below); **16 native goldens byte-identical** (all new emission
gated; native server + FFI TUs `#if !defined(__wasm__)` guarded, excluded from the reactor); 4 CI
gates green incl. Windows MSVC; executable `.tks` proof per surface on **native AND WASM** (server
tokens: native real + WASM synthetic/WASI); no dead tokens; the human merges the principal PR;
**English only**.

**SAST focus (now centred on the FFI boundary):**
- **Marshalling** — teko value ↔ C ABI: buffer pointer+length correctness, NUL-termination, no
  reading past a returned buffer, width-correct casts (`intptr_t`/`int32_t`, Windows LLP64),
  signed/unsigned at the boundary.
- **Lifetimes / ownership** — who frees (the lib vs teko); **`calloc` zero-init + clear ownership; no
  UAF/double-free.** The prior **~50% Windows crash was a wild-free in `src/parser_ffi.c`** that only
  **TSan** caught — keep the TSan gate and treat every new FFI binding as that class of risk.
- **Error mapping** — every system-lib return code is checked and mapped to a teko fail-loud error;
  never ignore a negative/NULL return; no silent partial reads/writes.
- **Native parser bounds** — HTTP request/response, WS frames, the router's path/method match, and
  the DEFLATE core are **attacker-controlled** surfaces: bounds/length checks before every copy;
  decompression-bomb caps; integer-overflow-safe sizing; accept loop hardened vs fd-exhaustion/
  slowloris (timeouts, caps). No format-string/path-traversal on URLs/headers/routes.
- Compression kept opt-in, never auto-applied to encrypted/secret payloads; new emission gated +
  feature-free output byte-identical.
