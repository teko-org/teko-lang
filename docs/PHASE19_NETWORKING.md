# Phase 19 — Native Networking & Web Architecture (CLIENT-only)

> **Status:** IN PROGRESS — branch `feat/phase-19-networking` (principal Draft PR #13 → `main`).
> Planned/managed under the **Orchestration Doctrine** (`docs/ORCHESTRATION_DOCTRINE.md`): the Master
> Agent (Opus) decomposes into breadcrumbs and delegates to Tech Lead (Sonnet) / Developer (Haiku)
> subagents; each breadcrumb ships as a sub-PR **into this phase branch** (PM merges after review +
> SAST + CI); the **owner (PO) merges the principal PR into `main`** at the end.

References — never duplicates — `docs/plan.md` §Phase 19, `CLAUDE.md` (bars), and the parallel
TLS-track doc `docs/PHASE19_TLS_TRACK.md`.

---

## 0. Owner-locked decisions (this revision)

- **A1 — pragmatic MVP.** Make every reserved web token live with uniform native+WASM proofs;
  advanced protocols become **parallel tracks**, not blanket deferrals.
- **C1 — cooperative async.** Client I/O integrates with the Phase-14 green-thread scheduler (a
  routine blocked on `recv` yields). Real async backends (io_uring/kqueue/IOCP) are a later perf
  track behind the *same* `net.*` surface — no new tokens.
- **B — CLIENT-ONLY, GLOBALLY (all targets).** The language makes **outbound** requests and **never
  runs a listening server**: **no `bind`/`listen`/`accept` anywhere**, on native OR WASM. The socket
  foundation is client-only (`connect`/`send`/`recv`/`close`). WASM net = host-import client
  (Node/WASI `fetch`/`ws`). This removes the "server-in-WASM is impossible" problem and makes every
  surface provable identically on native and WASM.
- **CI-trigger fix** stays in the setup commit (owner-approved).
- **TLS** deferred from the MVP critical path but runs as a **parallel track** with all SSL/TLS
  versions enumerated — see `docs/PHASE19_TLS_TRACK.md`.
- **Compression** folded in (owner request) as a tiered track — see §4.

## 1. Client-only semantics for the reserved web tokens (owner-confirmed framing)

The "no dead tokens" bar binds the reserved lexer tokens
`api middleware get post put delete patch head options rpc websocket` (+ bandwidth units
`kbps`/`mbps`/`gbps`). Under client-only semantics each is **an outbound-client concept**:

| Token(s) | Client-only meaning | Lowers to |
|---|---|---|
| `get post put delete patch head options` | **HTTP CLIENT** calls — `get(url)`, `post(url, body)`, … | HTTP client runtime over the socket client |
| `api` | a **declared client API binding/surface** (base URL + default headers/interceptors) | compile-time client binding descriptor |
| `middleware` | **client-side request/response interceptors** (auth, retry, logging) — a chain run around each call | interceptor chain over the HTTP client |
| `websocket` | **client** WebSocket (connect/send/recv/close) | WS client runtime (RFC 6455 client framing) |
| `rpc` | **client** RPC call (minimal length-prefixed request/response) | RPC client over the socket client |
| `kbps/mbps/gbps` | client throttle/QoS — `limit(10mbps)` on a call/connection | normalized bps literal consumed by a throttle surface |

`net`/`http`/`tls`/`compress`/`ws`/`rpc` low-level **dotted-identifier** surfaces (`net.tcp_connect`,
`http.get`, `tls.client`, `compress.gzip`) are **not tokens** — so QUIC/HTTP-2/3/4/full-gRPC create
**no dead tokens** when sequenced as later tracks. Greenfield: no socket/IO code exists today.

Every feature follows the house pattern: **portable C runtime (single source of truth + Unity KATs)
→ `OP_CALL_RUNTIME` id → native `teko_rt_*` wrapper + WASM reactor import → `.tks` proof on native
AND WASM**.

---

## 2. Parallelization — the dependency DAG (owner priority: MAXIMIZE concurrency)

The decisive observation: **every codec / protocol-state-machine is a pure, KAT-testable C runtime
with reference vectors and ZERO socket dependency.** Only *integration* (wiring a codec onto the live
client socket) serializes — and it funnels through one node (**T2**, socket wiring). So the bulk of
the work parallelizes immediately.

### Legend: ⟂ = independent (concurrent now) · → = depends on · ⏸ = deferred (reason)

### Wave 0 — start NOW, fully parallel (no deps beyond the existing repo)
Each is its own Tech Lead track → Dev breadcrumbs → its own sub-PR. They touch **separate**
`src/runtime/teko_*.c` + KAT files, so they don't collide (shared-list files handled in §2.1).

- **T1 — socket CLIENT foundation** `teko_socket.c`: `tcp_connect`/`udp_connect`/`send`/`recv`/
  `close`, handle table (calloc, fail-loud bounds), state machine, POSIX + Winsock. **No
  bind/listen/accept.** ⟂  *(root enabler for Wave 1)*
- **HTTP-CODEC** — request **builder** + response **parser** (status/headers/body, chunked) as pure
  byte↔struct functions, KAT'd. ⟂
- **WS-CODEC** — RFC 6455 client frame encode/decode + masking + UTF-8 validate, KAT'd. ⟂
- **C-CORE compression** — DEFLATE (RFC 1951) + zlib (RFC 1950) + gzip (RFC 1952) + CRC32/Adler-32,
  KAT'd vs RFC/zlib vectors. ⟂  *(also lands the `compress.*` surface plumbing)*
- **C-BR / C-ZSTD / C-LZMA / C-LZ4** — brotli (RFC 7932), zstd (RFC 8878), lzma/lzma2 (xz), lz4 —
  four independent codec tracks, each KAT'd. ⟂  *(codec ⟂ now; wire to `compress.*` once C-CORE
  surface lands — a 1-line append)*
- **TLS-RT** — TLS 1.3 (+1.2) key schedule (HKDF-Expand-Label), handshake state machine, record
  AEAD, driven by **RFC 8448 test vectors with no socket**. ⟂  *(see TLS-track doc)*
- **PROTOBUF-CODEC** — protobuf wire encode/decode (for gRPC), KAT'd. ⟂
- **H2-CODEC** — HTTP/2 framing + **HPACK** (RFC 7541), KAT'd. ⟂
- **QUIC-CODEC** — QUIC packet/frame codec (RFC 9000), KAT'd. ⟂  *(crypto binding waits on TLS-RT)*

### Wave 1 — after **T1 → T2** (socket wiring); parallel among themselves
- **T2 — socket wiring**: CMake + `teko_rt_socket_*` + reactor SRCS/EXPORTS + frontend `net.*`
  client surface + native/WASM emit + `.tks`/`.mjs` proofs.  → T1
- **HTTP-INT** `http.get/post/...` client over socket.  → T2 + HTTP-CODEC
- **WS-INT** client `websocket` connect/send/recv (handshake uses Phase-13 SHA-1 + base64).
  → T2 + HTTP-CODEC + WS-CODEC
- **RPC** minimal client length-prefixed call.  → T2
- **UNITS** `limit(10mbps)` throttle consuming the bandwidth literals.  → T2
- **TLS-INT** `tls.client` over socket (https).  → T2 + TLS-RT
- **COMPRESS-INT** Content-Encoding/Transfer-Encoding in the HTTP client + permessage-deflate in WS.
  → HTTP-INT + WS-INT + C-CORE

### Wave 2 — after their Wave-1 deps; parallel
- **ROUTER** — `api` client binding + `middleware` interceptor chain + the seven HTTP verbs as
  client calls.  → HTTP-INT   *(this is the token-completeness milestone for the verb set)*
- **H2-INT** — HTTP/2 client.  → TLS-INT (ALPN) + H2-CODEC
- **QUIC-INT** — QUIC client.  → T1(UDP) + TLS-RT + QUIC-CODEC

### Wave 3 — after Wave-2; parallel
- **GRPC** (full) — gRPC client.  → H2-INT + PROTOBUF-CODEC   *(the minimal `rpc` token is already
  live from Wave 1; this is the full wire format)*
- **H3-INT** — HTTP/3 client.  → QUIC-INT + QPACK-CODEC

### ⏸ Truly deferred (cannot be meaningfully parallelized **now**)
- **HTTP/4** — **no published standard / RFC exists.** Hard defer: a spec gap, not an effort
  question. Tracked as a placeholder only.
- **Async io_uring/kqueue/IOCP backends** — C1 locks cooperative-over-scheduler; the async backend
  only *replaces the blocking syscalls behind the same `net.*` surface*. It adds **no token/surface**,
  so it's a low-priority perf follow-up (isolatable, but nothing depends on it to go live).

### Capstone
- **CAPSTONE** `.tks` — outbound `net` + `http.get` + a routed `api`/`middleware` client call + a
  `websocket` client + a `compress.gzip` round-trip, proven on native AND WASM. → Wave 0–2.

### 2.1 Concurrency hygiene (so parallel tracks don't collide)
Only three files are shared across tracks: `CMakeLists.txt` (`CORE_SOURCES` + `teko_rt` lib),
`runtime/wasm/crypto/build-crypto-reactor.sh` (`SRCS` + `EXPORTS`), and the runtime-id→symbol table
(`src/codegen/emit_native_hosted.c`). Mitigations, set by the Master up front:
- **Pre-allocated `OP_CALL_RUNTIME` id ranges per track** (no two tracks pick the same id):
  net 60–79 · http 80–99 · ws 100–109 · rpc 110–119 · compress-core 120–139 · brotli 140–149 ·
  zstd 150–159 · lzma 160–169 · lz4 170–179 · TLS 180–219 · H2/HPACK 220–239 · QUIC 240–259 ·
  H3/QPACK 260–279 · gRPC/protobuf 280–299.
- **Append-only** edits to the CMake source lists and reactor `SRCS`/`EXPORTS` at distinct anchor
  comments, so merges are conflict-free; the PM resolves the rare list-append overlap at merge.
- Prefer `OP_CALL_RUNTIME` ids over new `OP_*` opcodes (avoids editing the shared opcode enum +
  both emit dispatchers — less contention than the duplex-style dedicated-opcode pattern).

---

## 3. Breadcrumb table (medium tasks → Tech Lead; small → Developer)

| Track | Wave | Role | Makes live | Deps |
|---|---|---|---|---|
| T1 socket client | 0 | Tech Lead | enabler | — |
| HTTP-CODEC | 0 | Tech Lead | (codec) | — |
| WS-CODEC | 0 | Developer | (codec) | — |
| C-CORE (deflate/zlib/gzip/crc/adler) | 0 | Tech Lead | `compress.*` core | — |
| C-BR brotli | 0 | Tech Lead | `compress.brotli` | C-CORE surface |
| C-ZSTD zstd | 0 | Tech Lead | `compress.zstd` | C-CORE surface |
| C-LZMA lzma/xz | 0 | Tech Lead | `compress.lzma` | C-CORE surface |
| C-LZ4 lz4 | 0 | Developer | `compress.lz4` | C-CORE surface |
| TLS-RT | 0 | Tech Lead | (runtime) | — |
| PROTOBUF-CODEC | 0 | Developer | (codec) | — |
| H2-CODEC (frame+HPACK) | 0 | Tech Lead | (codec) | — |
| QUIC-CODEC | 0 | Tech Lead | (codec) | — |
| T2 socket wiring | 1 | Tech Lead | `net.*` | T1 |
| HTTP-INT | 1 | Tech Lead | `http.*` | T2, HTTP-CODEC |
| WS-INT | 1 | Tech Lead | `websocket` | T2, HTTP-CODEC, WS-CODEC |
| RPC | 1 | Developer | `rpc` | T2 |
| UNITS throttle | 1 | Developer | `kbps/mbps/gbps` | T2 |
| TLS-INT | 1 | Tech Lead | `tls.*` | T2, TLS-RT |
| COMPRESS-INT | 1 | Tech Lead | (Content-Encoding/permessage-deflate) | HTTP-INT, WS-INT, C-CORE |
| ROUTER (api/middleware/verbs) | 2 | Tech Lead | `api middleware get post put delete patch head options` | HTTP-INT |
| H2-INT | 2 | Tech Lead | `http2.*` | TLS-INT, H2-CODEC |
| QUIC-INT | 2 | Tech Lead | `quic.*` | T1(UDP), TLS-RT, QUIC-CODEC |
| GRPC full | 3 | Tech Lead | `grpc.*` | H2-INT, PROTOBUF-CODEC |
| H3-INT | 3 | Tech Lead | `http3.*` | QUIC-INT, QPACK |
| CAPSTONE | — | Master | — | 0–2 |

Token-completeness milestone (every reserved web token + units live, native+WASM) = **end of Wave 2
ROUTER + Wave 1 WS-INT/RPC/UNITS** — reachable without TLS/H2/QUIC/H3/gRPC.

---

## 4. Compression tiering (owner request)

`src/runtime/teko_compress.c` (+ siblings) = single C source of truth, KAT'd vs RFC/zlib/reference
vectors. **`compress.*` is a free dotted surface** (verified: no reserved compression token —
`bundle`/`minify` are Phase-20 asset tooling, unrelated).

- **CORE (Phase 19, Wave 0):** DEFLATE (RFC 1951 inflate+deflate) + zlib (RFC 1950) + gzip
  (RFC 1952) + CRC32 + Adler-32. Needed for a credible HTTP/WS client (Content-Encoding,
  Transfer-Encoding, permessage-deflate).
- **PARALLEL tracks (independent sub-PRs):** brotli (RFC 7932, HTTP `br`), zstd (RFC 8878),
  lzma/lzma2 (xz), lz4.

### SAST / security (gate + docs)
Compression composed with encryption enables **CRIME/BREACH** compression-oracle attacks.
Therefore: (1) **never auto-compress secret/encrypted payloads by default** — compression is a
**separate, opt-in layer**; (2) **TLS 1.3 carries no record compression** (anti-CRIME by design) and
we keep it that way; (3) this is **safe composition only — it does NOT change the Phase-13 crypto
primitives.** Decompression is the highest-risk untrusted-input surface in the phase: enforce output
size caps (**decompression-bomb** guard), bounds-checked window/dictionary access, and integer-
overflow-safe length arithmetic in every inflate path.

---

## 5. Size estimate (revised)

KAT-driven C runtimes dominate; rough net LOC incl. tests/wiring/proofs:

- **A1 token-completeness core** (T1+T2, HTTP codec+int, ROUTER, WS codec+int, RPC, UNITS,
  compression CORE): **~5,000–7,000 LOC** — this is the milestone where *every reserved web token +
  units is live on native AND WASM*.
- **Compression parallel codecs** (brotli/zstd/lzma/lz4 + KATs): **~6,000–9,000 LOC** total.
- **TLS track** (TLS-RT + TLS-INT, 1.3 primary + 1.2 secondary): **~2,500–4,000 LOC**.
- **Advanced protocol tracks** (protobuf, H2+HPACK, gRPC, QUIC, H3+QPACK): **~10,000–14,000 LOC**.
- **Full parallel ambition** (everything above except the truly-deferred HTTP/4 + async backends):
  **~24,000–34,000 LOC** across ~12 concurrent Wave-0 tracks + downstream waves.

The owner's max-parallelism goal is served because ~12 Wave-0 tracks have **no inter-dependencies**
and can run as independent Tech Lead/Dev tracks immediately; serialization is confined to the
integration nodes (T2 and the per-protocol `*-INT` steps).

---

## 6. Bars (unchanged) + per-breadcrumb SAST gate
One increment/commit; ASan+UBSan **both** dispatch paths + TSan; **16 native goldens byte-identical**
(all new emission gated behind `uses_*`); 4 CI gates green incl. Windows MSVC (x86_64+arm64);
executable `.tks` proof per surface on **native AND WASM**; no dead tokens; the human merges the
principal PR; **English only**.

**SAST focus for this phase:** network and compressed input is **attacker-controlled** — every
parser (HTTP response, WS frame, every inflate path, TLS records, protobuf, HPACK/QPACK, QUIC
packets) is an injection/overflow surface. Enforce: bounds/length checks before every copy;
decompression output caps (bomb guard); integer-overflow-safe size arithmetic; `calloc` zero-init +
clear ownership (no UAF/double-free); width-correct casts incl. Windows LLP64 `intptr_t`/`int32_t`;
no format-string/path-traversal on URLs/headers; new emission gated + feature-free output
byte-identical; compression kept opt-in and never auto-applied to encrypted/secret payloads.
