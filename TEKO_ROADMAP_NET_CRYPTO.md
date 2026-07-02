# TEKO — ROADMAP: `teko::net::*` + `teko::crypto::*`

> **Status:** DESIGN (no code yet) · **Created:** 2026-07-02 · **Branch:** `feat/net-connectors` (off `chore/reboot`)
>
> Goal: network connectors for OSI **layers 4–7** under `teko::net::*` and a security surface under
> `teko::crypto::*`, **implemented 100% in Teko** (zero new C). Everything reaches the OS through the
> existing `extern fn … from "<lib>"` FFI (C7.1), guarded per-OS with `#os(...)`. Targets: **macOS**,
> **Linux (glibc + musl)**, **Windows**. Purpose: enable web + cloud-native projects in Teko.
>
> This doc is the **work-distribution contract**: it is decomposed so each unit (▪ = one agent task)
> is independently buildable, with explicit deps, file lists, the FFI symbols it binds, and its
> verification bar. Governed by the Laws (M.0–M.5); every `.c`/`.h`↔`.tks` pair honors the SUPREME RULE
> and every unit passes the verify-both gate ([[teko-verify-both-with-test-gate]]).

---

## 0. Ground rules (hold for every unit)

- **No C code.** New behavior is Teko (`.tks`) + `extern fn` bindings to platform libraries already on
  the system (libc / libSystem / `ws2_32` / `bcrypt`). We do **not** add `.c` to `src/runtime`. The only
  C-side change allowed is the **codegen/checker plumbing** needed to let raw externs accept the argument
  shapes networking needs (N-KEYSTONE below) — that is compiler machinery, not runtime C.
- **Sync first; async rides S8.** Blocking sockets are the first cut. The non-blocking / `Intent<T>`
  async surface is designed to layer on top later under the settled async model
  ([[teko-async-concurrency-design]]) — **not** in this roadmap. Signatures are chosen so the async
  variants are additive, never a rewrite.
- **VM honest-stops on externs.** The VM cannot run a raw platform `extern` (only `from "teko_rt"` is
  VM-backed). So: **pure-Teko logic is `.tkt`-tested on the VM; socket/crypto syscalls are proven by
  native regression examples.** Per-unit "verify" sections say which applies. This mirrors how
  `teko::time` and FFI already work ([[teko-ffi-extern]]).
- **Per-OS via `#os(...)`.** The `#os("linux")` / `#os("macos")` / `#os("windows")` function guard
  (C7.1f) already exists in the parser. glibc vs musl is **not** an `#os` axis — both are `"linux"` and
  share the same libc symbols (`socket`/`bind`/…); the only musl delta is the backtrace degradation
  already handled elsewhere, so **one Linux path covers both**. `.tkp [extern.libs.*]` declares the
  per-OS link libraries (`ws2_32` on Windows; none on POSIX).
- **Namespacing.** `teko::net` root holds **cross-protocol common items** (addresses, byte-order, socket
  handle type, errors, buffer helpers). **Each protocol is its own child namespace / file**:
  `teko::net::tcp`, `::udp`, `::tls`, `::http`, `::ws`, `::dns`. Same rule for crypto:
  `teko::crypto` root = common (bytes/hex/base64/constant-time-eq/errors); each primitive family is a
  child: `teko::crypto::hash`, `::hmac`, `::aead`, `::ecdh`, `::sign`, `::rand`.

---

## 1. What the compiler must gain first (KEYSTONE — blocks everything)

Networking and crypto pass **byte buffers** across the FFI boundary. Today `extern_type_ok`
(typer.tks:2153) allows only `Prim / Byte / Ptr / Uptr / extern-type-handle` for **raw** (non-`teko_rt`)
externs. `recv(fd, buf, len)` needs to hand C a pointer to a `[]byte`'s storage. Two settled facts make
this tractable:

- a `[]byte` already lowers to `typedef struct { uint8_t *ptr; uint64_t len; }` (teko_rt.h:62), and
  codegen can already read `.ptr`;
- Teko has **no index-assignment** (`buf[i] = x` is illegal, by ruling) — so a syscall cannot fill a
  Teko-owned buffer in place the way C does. This shapes the whole buffer strategy below.

### ▪ N-KEYSTONE — `C7.19`: byte-buffer transport across raw externs
**Deps:** none. **Files:** `src/checker/typer.{tks,c}`, `src/codegen/codegen.{tks,c}`, a smoke example.
**Decision to ratify (law-first, in the design PR):** choose ONE of
- **(A) `ptr<byte>` + explicit length**, with `teko::net` owning a small **arena-backed mutable byte
  region** whose address is taken via a new `buf_ptr(region) -> ptr<byte>` builtin (reuses the S1 arena;
  the region is written by C through the pointer, then read back by length). Keeps the extern ABI at the
  already-legal `ptr`/`uptr` — **smallest compiler change**, and the no-index-assign ruling is respected
  because the buffer is opaque to Teko until read back.
- **(B) allow `[]byte` as a raw-extern PARAM type** (not return): `extern_type_ok` accepts `Slice{Byte}`
  in parameter position; codegen emits `void *`/`uint8_t *` for the prototype and passes `(arg).ptr` at
  the call site. More ergonomic, slightly more codegen surface.

**Recommendation:** **(A)** — it is the minimal, ABI-stable change and composes with the arena memory
model already in place; (B) can be added later as sugar. The rest of this doc is written against a
`teko::net::buf` abstraction so the choice stays encapsulated.
**Verify:** a smoke example that calls a libc `extern` (`memset`/`read` on a temp file) filling a buffer
and reading it back, VM honest-stop + native roundtrip; both twins byte-identical.

---

## 2. `teko::net::*` — layers 4–7

### Layer 4 — transport

**▪ N0 — `teko::net` core (common).**
**Deps:** N-KEYSTONE. **Files:** `src/net/net.tks` (+ `.tkt`).
Contents: `IpAddr` (v4/v6 variant), `SocketAddr {ip, port}`, parse/format for both; host/network
**byte-order** helpers (`hton16/32`, `ntoh16/32` — pure Teko bit ops, VM-testable); the `Socket` handle
(an `extern type` opaque fd/SOCKET, or an `i64`/`uptr` wrapper); `NetError` (errno/WSA code + message,
built on the existing `error` value); the `Buf` byte-region abstraction from N-KEYSTONE (alloc, len,
as-slice, from-slice). **Verify:** `.tkt` on the VM for address parse/format + byte-order (100% pure
Teko, no syscalls).

**▪ N1 — `teko::net::tcp`.**
**Deps:** N0. **Files:** `src/net/tcp.tks`.
`TcpStream` (connect, read→`[]byte|error`, write, shutdown, close) + `TcpListener` (bind, listen,
accept→`TcpStream|error`). FFI: `socket/connect/bind/listen/accept/send/recv/setsockopt/close`
(`closesocket` + `WSAStartup` on Windows) guarded by `#os`. **Verify:** native regression
`net_tcp_loopback` (bind `127.0.0.1:0` → connect → send/recv → deterministic exit code).

**▪ N2 — `teko::net::udp`.**
**Deps:** N0. **Files:** `src/net/udp.tks`.
`UdpSocket` (bind, send_to, recv_from, connect, close). FFI: `sendto/recvfrom`. **Verify:** native
loopback datagram roundtrip.

### Layer 5/6 — session + presentation (TLS)

**▪ N3 — `teko::net::tls`.**
**Deps:** N1. **Files:** `src/net/tls.tks`.
A `TlsStream` wrapping a `TcpStream`, same read/write shape. **Design decision to ratify:** bind the
**OS-native TLS provider** per platform — Secure Transport / Network.framework (macOS), OpenSSL
(`libssl`, Linux; declared in `.tkp [extern.libs.linux]`), SChannel (Windows) — vs. implementing TLS in
pure Teko on top of `teko::crypto`. **Recommendation:** OS-native providers first (correctness/security
of a hand-rolled TLS is out of scope for the first cut); a pure-Teko TLS 1.3 can be a **later** unit once
`teko::crypto` AEAD/ECDH/sign land. **Verify:** native handshake against a loopback server using the
platform provider; certificate-verification behavior documented.

### Layer 7 — application

**▪ N4 — `teko::net::dns`.** **Deps:** N0. **Files:** `src/net/dns.tks`.
Name resolution via `getaddrinfo`/`freeaddrinfo` (all targets). Returns `[]SocketAddr | error`.
**Verify:** native resolve of `localhost` → `127.0.0.1`/`::1`.

**▪ N5 — `teko::net::http`.** **Deps:** N1 (client), N3 (https). **Files:** `src/net/http/*.tks`.
Pure-Teko HTTP/1.1: request/response types, header map, chunked + content-length bodies, a **client**
(`get`/`post` over TcpStream/TlsStream) and a minimal **server** (bind/accept/route→handler). URL
parser. HTTP/2 is a later unit. **Verify:** `.tkt` on the VM for the request/response **parser + encoder**
(pure Teko — the high-value, high-risk logic); native client GET against the N5 server over loopback.

**▪ N6 — `teko::net::ws`.** **Deps:** N5. **Files:** `src/net/ws.tks`.
RFC 6455: HTTP Upgrade handshake (`Sec-WebSocket-Accept` needs SHA-1 → **depends on `teko::crypto::hash`
C1**), frame codec (fin/opcode/mask/payload-len), masking. **Verify:** `.tkt` on the VM for the frame
codec + accept-key derivation (pure Teko); native echo over loopback.

> **Pre-reboot networking exists in `main` — DO NOT use it as a source (code OR syntax).** Two abandoned
> branches (`feat/phase-19-networking`, `feat/phase-19-ws-srv`, June 17) attempted a router/WS server via a
> **C/WASM reactor** — the exact opposite of this all-Teko mandate — and, critically, **their Teko syntax is
> pre-reboot and diverges heavily from the current language** (removed/renamed constructs). Agents must NOT
> open these to learn "how it was done": neither the implementation nor the syntax is valid. The ONLY
> portable thing is **raw external data** — RFC 6455 / RFC 8439 / NIST byte vectors (hex inputs, expected
> outputs) — which are language-agnostic. Take those from the RFCs directly, not from the old code.

---

## 3. `teko::crypto::*` — security surface

Crypto needs **wrapping (non-panicking) integer arithmetic** and **byte manipulation**. Two settled
constraints: `TEKO_OVERFLOW_DEBUG` guards int overflow (so crypto must use types/ops that wrap — confirm
`u32`/`u64` wrap in release, or add explicit `wrapping_add`-style helpers as a sub-unit), and there is
**no index-assignment**, so state arrays (hash blocks, key schedules) are built functionally or via the
`Buf` region from N0. **Ratify the wrapping-arithmetic story in the design PR before C-family code.**

**▪ C0 — `teko::crypto` core (common).** **Deps:** N-KEYSTONE. **Files:** `src/crypto/crypto.tks` (+`.tkt`).
Byte helpers: hex encode/decode, base64 (std + url), **constant-time equality** (`ct_eq`), byte-buffer
concat/slice on `Buf`. `CryptoError`. **Verify:** `.tkt` on the VM (all pure Teko — RFC test vectors).

**▪ C1 — `teko::crypto::hash`.** **Deps:** C0. **Files:** `src/crypto/hash.tks`.
SHA-256, SHA-512, SHA-1 (SHA-1 for WS accept-key only, marked legacy). **Decision:** pure-Teko
implementation (portable, VM-testable, no OS dep) — hashing is bounded and safe to hand-roll against
published vectors. **Verify:** `.tkt` on the VM against NIST/RFC test vectors (empty string, "abc", long
message). **Unblocks N6.**

**▪ C2 — `teko::crypto::hmac`.** **Deps:** C1. **Files:** `src/crypto/hmac.tks`. HMAC-SHA256/512
(RFC 2104). Pure Teko. **Verify:** `.tkt` RFC 4231 vectors.

**▪ C3 — `teko::crypto::rand`.** **Deps:** C0. **Files:** `src/crypto/rand.tks`.
CSPRNG bytes from the OS: `getentropy` (macOS/Linux), `getrandom` (Linux), `BCryptGenRandom` (Windows) via
`#os` externs. **Verify:** native — fills a `Buf`, asserts length + non-degenerate output (VM honest-stops).

**▪ C4 — `teko::crypto::aead`.** **Deps:** C0, (C3 for nonces). **Files:** `src/crypto/aead.tks`.
AES-GCM and/or ChaCha20-Poly1305. **Decision to ratify:** pure Teko vs OS provider — AEAD is
performance-sensitive and side-channel-sensitive; lean **OS/native provider** where available, pure-Teko
ChaCha20-Poly1305 as the portable fallback. **Verify:** `.tkt` (if pure) / native (if provider) against
RFC 8439 / NIST vectors.

**▪ C5 — `teko::crypto::ecdh` + `::sign`.** **Deps:** C0, C1. **Files:** `src/crypto/ecdh.tks`,
`src/crypto/sign.tks`. X25519 key agreement; Ed25519 sign/verify. **Decision:** OS provider vs pure Teko
(bignum field arithmetic is a large, delicate unit). **Recommendation:** defer to **after** C1–C4 land;
provider-backed first. **Verify:** RFC 7748 / 8032 vectors.

---

## 4. Dependency graph (build order)

```
N-KEYSTONE ─┬─ N0 ─┬─ N1 ─┬─ N3 ─┐
            │      │      └─ N5 ─┴─ N6         (N6 also needs C1)
            │      ├─ N2
            │      └─ N4
            └─ C0 ─┬─ C1 ─┬─ C2
                   │      └─ (unblocks N6)
                   ├─ C3
                   ├─ C4
                   └─ C5
```

**Parallelizable once their deps are green:** {N0, C0} after KEYSTONE; then {N1, N2, N4} ‖ {C1, C3};
then {N3, N5} ‖ {C2, C4}; then {N6, C5}. A migration/DRY pass is **not** part of this roadmap (Phase 11).

## 5. Per-unit agent contract (what each task hands back)

Every ▪ unit is one agent task producing: (1) the `.tks` file(s) + C twin where the compiler is touched
(KEYSTONE only), each with a justification header; (2) `.tkt` tests for pure-Teko logic AND/OR a
`examples/regressions/<name>/` native example, per the unit's "Verify"; (3) `.tkp [extern.libs.*]`
entries for any new link libraries; (4) a green **verify-both** run (`./build/teko build . -o bin` +
`./bin/teko build . -o /tmp/gen2` + byte-identical C + gen-2==gen-3) or, for compiler-touching units, the
full self-host fixpoint. Agents **DRAFT**; integration + any law tension → tribunal ([[teko-orchestration-model]]).

## 6. Open decisions to ratify in the design PR (before any C-family/socket code)

1. **N-KEYSTONE**: buffer transport (A) `ptr<byte>`+arena `Buf` vs (B) `[]byte` raw-extern param. *(rec: A)*
2. **Socket handle representation**: `extern type` opaque handle vs `i64`/`uptr` wrapper. *(rec: opaque `extern type`)*
3. **TLS (N3) + AEAD/ECDH (C4/C5)**: OS-native provider vs pure Teko. *(rec: provider-first; pure-Teko fallback later)*
4. **Wrapping arithmetic for crypto** under `TEKO_OVERFLOW_DEBUG`: confirm release-mode wrap vs add explicit `wrapping_*` helpers. *(must resolve before C1)*
5. **Async surface**: confirmed **out of scope** here; signatures shaped so S8 `Intent<T>` variants are additive.
