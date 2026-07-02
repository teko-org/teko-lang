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
  `teko::net::{tcp,udp,unix,tls,dtls,dns,http,http2,http3,ws,sse,grpc,mqtt,amqp,redis,smtp,imap,pop3,
  ftp,ssh,proxy,quic}`. Same rule for crypto: `teko::crypto` root = common (bytes/ct-eq/errors); each
  family a child: `teko::crypto::{hash,mac,kdf,password,cipher,aead,rand,pk,x509}` (+ app helpers). Shared
  serialization lives under a sibling `teko::encoding::{json,protobuf,asn1,cbor}` (§2b) since it is reused
  well beyond networking. The full enumerated set + tiers is in §2/§2b/§3/§4 — this is not exhaustive of
  everything, but it is the levantamento; new protocols slot in as additional child units.

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

**▪ N2b — `teko::net::unix` (Unix-domain sockets, AF_UNIX).** **Deps:** N0. **Files:** `src/net/unix.tks`.
Stream + datagram sockets over a filesystem path (`sockaddr_un`) — the backbone of local IPC and
cloud-native sidecar patterns (e.g. talking to a container runtime / proxy socket). POSIX only
(`#os` excludes Windows in the first cut; Windows AF_UNIX is a later add). **Verify:** native loopback
path roundtrip.

> **Tier note:** N1/N2/N2b are the transport **first cut**. **Later transport units (T2):** raw sockets
> (`SOCK_RAW`, for tooling), and **QUIC** (`teko::net::quic`) — a layer-4 transport over UDP that is really
> its own large sub-project (needs TLS 1.3 + congestion control); it gates HTTP/3 and is a T2 keystone of
> its own. SCTP is out of scope (rare, uneven OS support).

### Layer 5/6 — session + presentation (TLS/DTLS)

**▪ N3 — `teko::net::tls` (TLS 1.2 + 1.3).**
**Deps:** N1. **Files:** `src/net/tls.tks`.
A `TlsStream` wrapping a `TcpStream`, same read/write shape; client + server; SNI, ALPN (needed by
HTTP/2 + gRPC), session resumption. **Design decision to ratify:** bind the **OS-native TLS provider**
per platform — Secure Transport / Network.framework (macOS), OpenSSL (`libssl`, Linux; declared in
`.tkp [extern.libs.linux]`), SChannel (Windows) — vs. implementing TLS in pure Teko on top of
`teko::crypto`. **Recommendation:** OS-native providers first (a hand-rolled TLS is a security liability
for the first cut); a pure-Teko TLS 1.3 becomes a **later (T2)** unit once `teko::crypto` AEAD/ECDH/sign/
x509 land. **Verify:** native handshake against a loopback server; certificate-verification behavior
documented.

**▪ N3b — `teko::net::dtls` (T2).** **Deps:** N2, N3. Datagram TLS over UDP — needed by QUIC-adjacent and
WebRTC use cases. Later tier.

### Layer 7 — application

**First cut (T1) — the web + cloud-native baseline:**

**▪ N4 — `teko::net::dns`.** **Deps:** N0. **Files:** `src/net/dns.tks`.
Name resolution via `getaddrinfo`/`freeaddrinfo` (all targets). Returns `[]SocketAddr | error`.
Later (T2): a pure-Teko resolver with **DoH** (DNS-over-HTTPS, needs N5) and **DoT** (DNS-over-TLS, needs
N3) for cloud environments that mandate encrypted DNS. **Verify:** native resolve of `localhost`.

**▪ N5 — `teko::net::http` (HTTP/1.1).** **Deps:** N1 (client), N3 (https). **Files:** `src/net/http/*.tks`.
Pure-Teko HTTP/1.1: request/response types, header map, chunked + content-length bodies, a **client**
(`get`/`post`/… over TcpStream/TlsStream) and a minimal **server** (bind/accept/route→handler). URL
parser. Cookies, redirects, keep-alive. **Verify:** `.tkt` on the VM for the request/response
**parser + encoder** (pure Teko — high-value, high-risk); native client GET against the N5 server.

**▪ N6 — `teko::net::ws` (WebSocket, RFC 6455).** **Deps:** N5, **C1 (SHA-1 for the accept-key)**.
**Files:** `src/net/ws.tks`. HTTP Upgrade handshake, frame codec (fin/opcode/mask/payload-len), masking,
close/ping/pong. **Verify:** `.tkt` on the VM for the frame codec + accept-key derivation; native echo.

**▪ N7 — `teko::net::sse` (Server-Sent Events).** **Deps:** N5. A thin `text/event-stream` codec on top of
HTTP (client + server helper). Small but distinct L7 unit; common in cloud dashboards. **Verify:** `.tkt`
for the event parser/encoder; native stream over loopback.

**Cloud-native tier (T2) — the messaging + RPC + store protocols:**

**▪ N8 — `teko::net::http2`.** **Deps:** N3 (ALPN), needs **HPACK** header compression + the binary framing
layer + stream multiplexing. Gates gRPC. Large unit. **Verify:** `.tkt` for HPACK + framing vectors
(RFC 7541/7540); native request against a loopback H2 server.

**▪ N9 — `teko::net::grpc`.** **Deps:** N8, **S-PB (protobuf, §4b)**. Unary + streaming RPC over HTTP/2,
length-prefixed protobuf messages, status codes/metadata. The cloud-native RPC baseline. **Verify:**
`.tkt` for the message-framing codec; native unary call over loopback.

**▪ N10 — `teko::net::http3`.** **Deps:** QUIC (T2 transport), **QPACK**. HTTP over QUIC. Later tier,
gated by the QUIC sub-project. **Verify:** native GET against a loopback H3 server.

**▪ N11 — `teko::net::mqtt`.** **Deps:** N1 (+N3 for TLS). MQTT 3.1.1 + 5.0 client (connect/publish/
subscribe/QoS 0-2). IoT + cloud eventing. **Verify:** `.tkt` for the control-packet codec; native
pub/sub against a loopback broker stub.

**▪ N12 — `teko::net::amqp`.** **Deps:** N1 (+N3). AMQP 0-9-1 client (RabbitMQ-style: channels/exchanges/
queues/publish/consume). **Verify:** `.tkt` for the frame codec; native against a loopback stub.

**▪ N13 — `teko::net::redis` (RESP2/RESP3).** **Deps:** N1 (+N3). The Redis serialization protocol —
a very common cloud cache/store client. Pure-Teko RESP codec + command layer. **Verify:** `.tkt` for the
RESP codec (well-specified, high-value); native against a loopback stub.

**Classic-internet tier (T3) — mail, transfer, shell, proxy:**

**▪ N14 — `teko::net::smtp` / `::imap` / `::pop3`** (email send + retrieve; STARTTLS via N3).
**▪ N15 — `teko::net::ftp`** (control + data channels; FTPS via N3).
**▪ N16 — `teko::net::ssh`** client (transport + auth + channels; heavy — depends on `teko::crypto` pk +
aead + kex; likely OS-provider-assisted or a large later unit).
**▪ N17 — `teko::net::proxy`** — SOCKS5 + HTTP `CONNECT` tunneling (needed to route any of the above through
corporate/cloud proxies). Small, high-leverage; **Verify:** `.tkt` for the SOCKS5 handshake codec.

> **Pre-reboot networking exists in `main` — DO NOT use it as a source (code OR syntax).** Two abandoned
> branches (`feat/phase-19-networking`, `feat/phase-19-ws-srv`, June 17) attempted a router/WS server via a
> **C/WASM reactor** — the exact opposite of this all-Teko mandate — and, critically, **their Teko syntax is
> pre-reboot and diverges heavily from the current language** (removed/renamed constructs). Agents must NOT
> open these to learn "how it was done": neither the implementation nor the syntax is valid. The ONLY
> portable thing is **raw external data** — RFC / NIST byte vectors (hex inputs, expected outputs) — which
> are language-agnostic. Take those from the RFCs directly, not from the old code.

## 2b. Serialization / encoding infrastructure (several protocols depend on these)

Greenfield in the corpus (no JSON / protobuf / ASN.1 today — verified). Each is pure Teko, fully
VM-`.tkt`-testable, and reusable well beyond networking.

**▪ S-JSON — `teko::encoding::json`.** **Deps:** none. Parser + encoder (RFC 8259). Needed by HTTP APIs,
config, DoH, many cloud services. High priority, unblocks a lot.
**▪ S-PB — `teko::encoding::protobuf`.** **Deps:** none for the wire codec; a `.proto` compiler is a
separate later tool. Varint/zigzag/length-delimited wire format. **Gates gRPC (N9).**
**▪ S-ASN1 — `teko::encoding::asn1`.** **Deps:** none. DER/BER encode+decode + PEM framing. **Gates X.509
(`teko::crypto::x509`), RSA/EC key parsing.**
**▪ S-CBOR — `teko::encoding::cbor` (T3).** Binary JSON-ish; used by COSE/WebAuthn and some IoT.
**▪ S-URL/S-MIME helpers** — form-urlencoded, multipart/form-data (ride with N5 HTTP).

---

## 3. `teko::crypto::*` — security surface

Crypto needs **wrapping (non-panicking) integer arithmetic** and **byte manipulation**. Two settled
constraints: `TEKO_OVERFLOW_DEBUG` guards int overflow (so crypto must use types/ops that wrap — confirm
`u32`/`u64` wrap in release, or add explicit `wrapping_add`-style helpers as a sub-unit), and there is
**no index-assignment**, so state arrays (block state, key schedules, S-boxes) are built functionally or
via the `Buf` region from N0. **Ratify the wrapping-arithmetic story in the design PR before any
C-family code** (it blocks every hand-rolled primitive).

Each family is its OWN child namespace/file. Legacy primitives (MD5, SHA-1, DES/3DES, RC4, PKCS#1v1.5
enc) are provided ONLY where a real protocol still needs them, and are marked `legacy` in the docs/API.

**▪ C0 — `teko::crypto` core (common).** **Deps:** N-KEYSTONE. **Files:** `src/crypto/crypto.tks` (+`.tkt`).
Byte helpers on `Buf`, **constant-time equality** (`ct_eq`) + constant-time select, secure-zero,
`CryptoError`. (Hex/base64/base32 live in `teko::encoding` §2b and are re-exported.) **Verify:** `.tkt`.

### Hashes / MACs / KDFs (pure Teko — bounded, VM-testable, no OS dep)

**▪ C1 — `teko::crypto::hash`.** **Deps:** C0. **Files:** `src/crypto/hash.tks`.
SHA-2 (224/256/384/512), **SHA-3 + SHAKE128/256** (Keccak), **BLAKE2b/2s** (+ BLAKE3 as a stretch),
and `legacy` **SHA-1** (WS accept-key, X.509 legacy) + **MD5** (HTTP digest auth legacy). Streaming
(init/update/final) API. **Verify:** `.tkt` against NIST/RFC vectors. **Unblocks N6, C2, C6, S-ASN1 use.**

**▪ C2 — `teko::crypto::mac`.** **Deps:** C1, C4 (for GMAC/CMAC block-cipher MACs). **Files:**
`src/crypto/mac.tks`. **HMAC** (any hash, RFC 2104/4231), **Poly1305** (RFC 8439), **CMAC** + **GMAC**
(over AES). **Verify:** `.tkt` RFC vectors.

**▪ C3 — `teko::crypto::kdf`.** **Deps:** C1, C2. **Files:** `src/crypto/kdf.tks`.
**HKDF** (RFC 5869), **PBKDF2** (RFC 8018). Key derivation used pervasively by TLS/HTTP-auth/tokens.
**Verify:** `.tkt` RFC vectors.

**▪ C3b — `teko::crypto::password`.** **Deps:** C3. **Files:** `src/crypto/password.tks`.
Password hashing / verification: **Argon2id** (recommended), **scrypt**, `legacy` **bcrypt**. Memory-hard
functions — pure Teko, but heavy; T2. **Verify:** `.tkt` reference vectors + roundtrip verify.

### Symmetric ciphers (the AES gap — first-class now)

**▪ C4 — `teko::crypto::cipher` (block + stream ciphers).** **Deps:** C0. **Files:** `src/crypto/cipher.tks`.
**AES-128/192/256** core (key schedule + encrypt/decrypt block), with modes **CBC, CTR, CFB, OFB, ECB**
(ECB marked discouraged) and **AES-KW** key-wrap (RFC 3394); **ChaCha20** stream cipher (RFC 8439);
`legacy` **3DES**. **Decision to ratify:** pure-Teko AES (portable, but must be **constant-time / no
data-dependent table lookups** — a real side-channel concern) vs an OS/AES-NI-backed provider.
**Recommendation:** pure-Teko bitsliced/constant-time AES for portability, provider path optional later.
**Verify:** `.tkt` against NIST FIPS-197 / SP 800-38A vectors. **Unblocks C5 AEAD + C2 CMAC/GMAC.**

**▪ C5 — `teko::crypto::aead`.** **Deps:** C4, C2 (Poly1305), C6 (rand for nonces). **Files:**
`src/crypto/aead.tks`. **AES-GCM**, **AES-CCM**, **ChaCha20-Poly1305** (RFC 8439), **AES-GCM-SIV**
(nonce-misuse-resistant). The AEAD layer every modern protocol (TLS 1.3, QUIC, WireGuard-style) needs.
**Verify:** `.tkt` / native against RFC 8439 / NIST vectors.

### Randomness + public-key + certificates

**▪ C6 — `teko::crypto::rand`.** **Deps:** C0. **Files:** `src/crypto/rand.tks`.
CSPRNG bytes from the OS: `getentropy` (macOS/Linux), `getrandom` (Linux), `BCryptGenRandom` (Windows) via
`#os` externs. Optional DRBG (HMAC/CTR) on top. **Verify:** native (VM honest-stops on the extern).

**▪ C7 — `teko::crypto::pk` (public-key).** **Deps:** C0, C1, S-ASN1 (key/cert encoding), C6. **Files:**
`src/crypto/pk/*.tks`. Split into sub-units, T2/T3, each its own agent task:
- **RSA** — OAEP + PSS (+ `legacy` PKCS#1 v1.5); key parse/gen.
- **ECDSA** — P-256 / P-384 / P-521.
- **EdDSA** — Ed25519 (+ Ed448).
- **ECDH** — P-256 / P-384; **X25519** / **X448**.
- **DH** — classic finite-field (`legacy`).
These need **big-integer / field arithmetic** (a shared `teko::crypto::bignum` sub-module — its own unit,
constant-time) — a large, delicate body of work. **Decision to ratify:** OS/provider-backed first vs pure
Teko. **Recommendation:** provider-backed for the first cut; pure-Teko X25519/Ed25519 (self-contained,
well-specified) as the earliest hand-rolled pieces. **Verify:** RFC 7748/8032, FIPS 186 vectors.

**▪ C8 — `teko::crypto::x509`.** **Deps:** S-ASN1, C7, C1. Certificate + CSR parse/build, chain
verification, name/SAN matching — needed by a pure-Teko TLS and by cert tooling. T2/T3. **Verify:** `.tkt`
parse of real DER cert fixtures + chain-validation cases.

**▪ C9 — application security helpers (T3).** **Deps:** C1/C2/C3/C7 + S-JSON/S-CBOR. **JWT** (JWS/JWE),
**COSE**, TOTP/HOTP (RFC 6238/4226). High-level, built entirely on the primitives above.

---

## 4. Dependency graph + tiers (build order)

`S-*` = serialization (§2b), `N*` = net (§2), `C*` = crypto (§3). KEYSTONE blocks all runtime FFI units;
the pure-Teko serialization + hash/cipher units do NOT need the keystone and can start immediately.

```
KEYSTONE ── N0 ─┬─ N1 ─┬─ N3(TLS) ─┬─ N5(HTTP) ─┬─ N6(WS)   [needs C1]
                │      │           │            ├─ N7(SSE)
                │      │           │            └─ N8(H2) ── N9(gRPC) [needs S-PB]
                │      │           └─ N4(DNS→DoH/DoT)
                │      ├─ N2(UDP) ── QUIC ── N10(H3)
                │      ├─ N2b(unix)
                │      └─ N11 MQTT / N12 AMQP / N13 Redis / N14 mail / N15 ftp / N16 ssh / N17 proxy
                └─ C6(rand)

(no keystone needed — pure Teko, start now)
 S-JSON · S-PB · S-ASN1 · S-CBOR
 C0 ─┬─ C1(hash) ─┬─ C2(mac) ─┬─ C3(kdf) ── C3b(password)
     │            └─ (unblocks N6)
     └─ C4(cipher=AES/ChaCha) ─┬─ C5(aead) ─┐
                               └─ C2 GMAC/CMAC
 S-ASN1 + C1 + C6 ── C7(pk: RSA/ECDSA/EdDSA/ECDH via bignum) ── C8(x509)
 C1/C2/C3/C7 + S-JSON ── C9(JWT/COSE/TOTP)
```

**Tiers.** **T1 (first cut — web baseline):** KEYSTONE, N0, N1, N2, N3(provider TLS), N4, N5, N6, N7,
S-JSON; C0, C1, C2, C3, C4(AES+ChaCha), C5(AEAD), C6(rand). **T2 (cloud-native):** N2b, N8(H2), N9(gRPC),
N11(MQTT), N12(AMQP), N13(Redis), DoH/DoT, S-PB, S-ASN1; C3b(password), C7(pk), C8(x509). **T3 (breadth):**
QUIC/N10(H3), N14(mail), N15(ftp), N16(ssh), N17(proxy), S-CBOR, C9, pure-Teko TLS 1.3.

**Parallelizable now (no keystone):** {S-JSON, C0}; then {C1, C4} ‖ {S-PB, S-ASN1}; then {C2, C5, C3}.
**Parallel after KEYSTONE:** {N0, C6}; then {N1, N2, N2b, N4} once N0 is green; then the L7 fan-out.
A migration/DRY pass is **not** part of this roadmap (Phase 11).

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
3. **Provider vs pure Teko** for the security-critical/perf-critical units — **TLS (N3), AES (C4), AEAD (C5), public-key (C7)**: OS-native provider vs hand-rolled Teko. *(rec: provider-first for TLS/RSA/ECDSA; pure-Teko constant-time AES/ChaCha/AEAD + X25519/Ed25519, which are self-contained and well-specified)*
4. **Wrapping arithmetic for crypto** under `TEKO_OVERFLOW_DEBUG`: confirm release-mode wrap vs add explicit `wrapping_*` helpers. *(must resolve before ANY C-family primitive — blocks C1/C4)*
5. **Constant-time guarantee**: does Teko need a `ct`-typed discipline / secret-int type, or is it a hand-audited convention per unit? (AES table lookups, `ct_eq`, bignum) *(rec: convention + audit first; a language feature is a separate proposal)*
6. **Scope/tier cut for v1**: confirm T1 is the release target and T2/T3 land incrementally. Which L7 protocols are must-have for the first web/cloud milestone (HTTP+WS+TLS+JSON+gRPC?) vs later.
7. **Async surface**: confirmed **out of scope** here; signatures shaped so S8 `Intent<T>` variants are additive.
8. **Big-integer module** (`teko::crypto::bignum` / a general `teko::math::bigint`?): crypto public-key needs it — decide whether it lives under crypto or as a general-purpose numeric module reused elsewhere.
