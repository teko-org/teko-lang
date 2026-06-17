# Phase 19 — TLS / SSL Track (FFI to system TLS; native client+server, WASM client-first)

> Parallel track under Phase 19 (`docs/PHASE19_NETWORKING.md`). **Under the NativeAOT/FFI posture,
> Phase-19 TLS is FFI-ONLY — we bind the OS's audited system TLS library; we do NOT roll our own
> TLS.** The from-scratch native TLS 1.3/1.2 stack (RFC 8448) is moved to **Future targets** (it only
> existed to justify a static/musl mode — also deferred). **The Phase-13 crypto primitives are kept
> as-is, unchanged — they are not TLS** and this track does not touch them.

**Target split:** native wires `tls.client` **and** `tls.server` (the latter over the native accept
loop, T1b) through the system TLS lib; **WASM** wires `tls.client` via host-import (Node TLS) — and
WASI-TLS where the runtime provides it. Browser = client only.

---

## 1. All SSL/TLS versions — enumerated, with posture

Even though we FFI a system library, we **configure** it to negotiate only the secure versions and
**disable** the rest. This table is the configuration contract for every backend.

| Protocol | Year / RFC | Posture in Teko | Rationale |
|---|---|---|---|
| **SSL 2.0** | 1995 | **DISABLED — never negotiated** | Broken; **RFC 6176 prohibits**. |
| **SSL 3.0** | 1996 / RFC 6101 | **DISABLED — never negotiated** | **POODLE**; **RFC 7568 prohibits**. |
| **TLS 1.0** | 1999 / RFC 2246 | **DISABLED** | BEAST; **deprecated by RFC 8996**. |
| **TLS 1.1** | 2006 / RFC 4346 | **DISABLED** | Superseded; **deprecated by RFC 8996**. |
| **TLS 1.2** | 2008 / RFC 5246 | **ENABLED (secondary)** | Still widely required. Configure **AEAD-only** suites (ECDHE + AES-GCM / ChaCha20-Poly1305); no CBC/RC4/static-RSA. |
| **TLS 1.3** | 2018 / RFC 8446 | **ENABLED (primary)** | Preferred. AEAD-only; **no record compression** (anti-CRIME). |

### Datagram TLS (for the UDP/QUIC FFI track — noted)
| Protocol | Year / RFC | Posture |
|---|---|---|
| **DTLS 1.0** | 2006 / RFC 4347 | DISABLED (maps to TLS 1.1). |
| **DTLS 1.2** | 2012 / RFC 6347 | Optional future (UDP), AEAD-only. |
| **DTLS 1.3** | 2022 / RFC 9147 | Optional future. **QUIC uses the TLS 1.3 handshake (RFC 9001)**, supplied by the QUIC FFI lib (quiche/msquic), not by this track. |

**Target set:** **TLS 1.3 (primary) + TLS 1.2 (secondary), AEAD-only**, configured on whichever
system backend is chosen (§2). Everything ≤ TLS 1.1 and all SSL is disabled at config time.

---

## 2. PENDING SUB-DECISION (record now — DO NOT resolve until the TLS track starts)

**Which system-TLS backend strategy?** (the owner **leans platform-native**)

- **Option P — platform-native (3 thin backends, ZERO extra dependency):**
  **SChannel** (Windows) · **Secure Transport / Network.framework** (macOS/Darwin) · **OpenSSL**
  (Linux glibc). Each is already present on its OS → no shipped dependency. Cost: three thin FFI
  backends behind the one `tls.*` surface (the clock-pattern, ×3).
  *Note: Apple has deprecated Secure Transport in favour of Network.framework — to evaluate when the
  track starts.*
- **Option O — OpenSSL-everywhere (1 backend):** one OpenSSL FFI binding for all targets. Cost: needs
  **OpenSSL present on macOS and Windows** (a shipped/managed dependency), contradicting the
  "dynamically link what each OS already provides" posture on mac/Windows.

**Owner lean:** Option P (platform-native). **Status: NOT resolved** — recorded as the first decision
the TLS track makes. Everything else in this doc is backend-agnostic (one `tls.*` surface; the
version contract in §1 applies to whichever backend wins).

This fork is the **`system`-backend strategy** within the locked FFI-CORE backend hierarchy
(`docs/PHASE19_NETWORKING.md` §0.3): the **`bundled` default fallback for TLS = BoringSSL/OpenSSL
(vendored)** regardless of the fork; the fork only decides what the **`system`** tier probes for per
OS (platform-native SChannel/Secure-Transport/OpenSSL vs OpenSSL-everywhere). TLS is **lazy by use** —
a program that never calls `tls.*` links no TLS library at all.

---

## 3. TLS-FFI breadcrumbs (Wave 1, once FFI-CORE lands)
1. **`tls.client`** — connect over `net.tcp_connect`, hand the fd to the system TLS lib, configure
   the §1 version/suite contract, verify the server cert chain (the lib's verifier + a trust-store
   policy sub-decision), then `tls.send`/`tls.recv`.
2. **`tls.server` (native)** — TLS over the native accept loop (T1b) with a configured cert+key;
   `https` server = HTTP-SRV over `tls.server`. No browser server (WASI where supported).
3. **`https` client** = HTTP-INT over `tls.client`.
4. **Marshalling/lifetime/error mapping** through **FFI-CORE** (see networking doc §7): buffer+length
   correctness, ownership of lib-allocated handles (no UAF/double-free), every return code mapped to
   a teko fail-loud error.

**Proofs.** Native: `tls.client` against a local `tls.server` loopback (and/or a known endpoint);
`tls.server` accept+handshake loopback. WASM: `tls.client` via host-import TLS (Node TLS).

---

## 4. SAST notes (gate — FFI boundary + records)
The system TLS lib is audited, so our risk is the **binding**: correct buffer/length marshalling,
ownership of lib-allocated SSL/context handles (**no UAF/double-free** — the prior wild-free was in
`parser_ffi.c`; keep TSan), every `SSL_*`/SChannel/Secure-Transport return code checked and mapped,
constant-time-agnostic (the lib owns CT). Configure: reject ≤TLS-1.1/SSL and non-AEAD suites; enable
cert + hostname verification by default; **no record compression** (anti-CRIME). `calloc` zero-init +
clear ownership across the handshake/session state we hold.
