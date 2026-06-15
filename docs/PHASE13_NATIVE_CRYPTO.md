# Phase 13 — Native Cryptography (design & plan)

> Status: **current** (branch `feat/phase-13-native-crypto`). Dedicated, owner-requested
> phase. Implements the widest practical set of **symmetric** and **asymmetric** ciphers
> **fully native — no OpenSSL, no external libraries** — validated by known-answer test
> vectors (NIST / RFC KATs) + round-trips. Phase 17 (Networking & Web) later consumes these
> primitives for TLS 1.3. See `docs/plan.md` → "PHASE 13: Native Cryptography".

## Surface (Teko keywords — reserved in Phase 12, lowered here)
`crypto`, `hash`, `encrypt`/`decrypt`, `sign`/`verify`, plus `encode`/`decode` interop with
the Phase 12 base codecs (`base64`/`base32`/`hex`). Each primitive lands with **grammar +
functional native logic + executable KAT tests** — never a dead token.

## Architecture decision — a native C runtime library is the single source of truth

The Phase 12-G base codecs were small enough to hand-emit as on-demand WAT (`emit_wasm.c`,
`OP_CALL_RUNTIME`). That approach **does not scale** to SHA-256 / Keccak / AES / Curve25519
across the 16 native emitters + WASM — hand-writing Keccak in WAT, or in machine code per
emitter, is infeasible and unverifiable.

Instead, every primitive is implemented **once, in portable C23**, in the **embedded native
runtime** (`src/runtime/teko_crypto_*.c`) — the same place Phase 8's concurrency runtime
(`teko_arena`/`teko_channel`/`teko_thread`) lives. This C library is:

- **The correctness gate.** Each primitive is KAT-tested directly in the Unity suite
  (`tests/runtime/test_crypto_*.c`) against NIST/RFC vectors + round-trips, independent of
  any language wiring. Pure, deterministic, MSVC-portable, trivially sanitizer-clean.
- **Linked into produced native binaries** via `tld`, exactly as the concurrency runtime is
  (the "Teko Core Runtime", Phase 8). The `hash`/`crypto`/`sign`/`verify` tokens lower to
  calls into it.
- **The basis for the WASM surface.** Where a primitive is small and hot (hashes, ChaCha20)
  it can additionally be emitted as on-demand WAT (the Phase 12-G pattern) and proved with
  assemble+run; otherwise the native C library remains the reference and WASM lowering is
  sequenced per primitive. WASM-emitted variants get their own `assemble + run` proof — never
  `strstr`-only.

**This decouples two concerns, each independently testable, one increment per commit:**
1. **Primitive correctness** — KAT-tested C in `src/runtime/`. (Built first, per primitive.)
2. **Language wiring** — grammar + lowering + an end-to-end test that the token produces the
   right bytes through the compiler.

This is the big architectural call for the phase; it is flagged for owner confirmation. The
alternative (emit each primitive per-backend) is rejected as infeasible/unverifiable.

## Sub-phase order (implementation order = dependency order)

Confirmed to follow the dependency chain (matches `docs/plan.md`'s "Build order vs.
numbering" note: hashes → CSPRNG + HKDF/PBKDF2 → symmetric/AEAD → asymmetric, RSA last):

| Order | Sub-phase | Primitives | Viability |
|-------|-----------|-----------|-----------|
| **13.1** | Hashes & MAC | SHA-256, SHA-512, SHA-3 (Keccak/SHAKE), BLAKE3, HMAC | **Most viable** — pure, fixed-width, KAT-friendly. Built first; everything else depends on it. |
| **13.3a** | CSPRNG + cheap KDF | platform CSPRNG (`getrandom`/`BCryptGenRandom`/`arc4random` + WASM host import), HKDF, PBKDF2 | Cheap once HMAC exists; needed before AEAD nonces / key derivation. |
| **13.2** | Symmetric / AEAD | ChaCha20-Poly1305 (RFC 8439) **first** (no hardware dep), then AES-128/192/256 CTR/CBC/GCM (software constant-time; GF(2¹²⁸) carryless mul for GCM) | ChaCha20-Poly1305 portable & easy; AES viable in constant-time software. **AES-NI/PCLMUL is a later optimization, not a correctness gate.** |
| **13.4** | Memory-hard KDF | scrypt, Argon2 | Harder (memory-hard); sequenced after AEAD. |
| **13.3b** | Asymmetric | X25519 + Ed25519 (Curve25519, fixed-field) **first**; then ECDSA/ECDH P-256/P-384; **RSA last** (PKCS#1 v1.5 / OAEP / PSS) | Curve25519 is the most viable native (no bignum). P-curves need modular field + point arithmetic. **RSA is hardest — needs a full multi-precision bignum (Montgomery mul, modexp); gated as the final piece.** |

(Numbering keeps the plan.md conceptual grouping; the *order* above is the build order.)

## Architectural gates flagged for the owner before they start
- **Constant-time AES strategy** (bitsliced vs. T-table-free table-free) — before 13.2 AES.
- **The bignum layer for RSA** (Montgomery multiplication, modexp, side-channel posture) —
  before 13.3b RSA. This is the heaviest single piece in the phase.

## Test-vector strategy
- **Known-answer tests (KATs)** from the authoritative source per primitive: NIST CAVP /
  FIPS for SHA-2/SHA-3/AES/GCM/ECDSA/P-curves; RFC 8439 for ChaCha20-Poly1305; RFC 7748
  (X25519) / RFC 8032 (Ed25519); RFC 5869 (HKDF); RFC 6070 (PBKDF2); the BLAKE3 reference
  vectors; RFC 7914 (scrypt) / RFC 9106 (Argon2). Vectors are embedded as `static const`
  byte arrays in the runtime tests — no network, no fixtures.
- **Round-trips** for every reversible op (encrypt→decrypt, sign→verify, encode→decode).
- **Negative tests**: AEAD tag mismatch must reject; signature verify must reject tampered
  messages; bad padding must reject.
- Discipline unchanged: build Release + run suite under **ASan+UBSan on both dispatch paths**
  + **TSan** each commit; the 16 native emitter goldens never regress; any WASM-emitted
  variant is **assembled and run**, never `strstr`-only; nothing is done until all four CI
  gates are green.

## Where it lives (file map)
- `src/runtime/teko_crypto_*.c` / `.h` — the portable primitives (one translation unit per
  family: `teko_crypto_sha2.c`, `teko_crypto_sha3.c`, `teko_crypto_blake3.c`,
  `teko_crypto_hmac.c`, …). Registered in `CMakeLists.txt` `CORE_SOURCES`.
- `tests/runtime/test_crypto_*.c` — Unity KAT suites; registered in `CMakeLists.txt` +
  `tests/test_main.c` `RUN_TEST` order.
- Frontend: lexer tokens already reserved; grammar/lowering wires the tokens to runtime
  calls (native) and, per primitive, optional WASM emission.

## Starting point
**13.1, SHA-256 first** — the smallest foundational primitive, FIPS 180-4 KATs. Lands as a
KAT-tested C runtime unit before any lowering, establishing the per-primitive template
(runtime unit → KAT test → CMake/RUN_TEST registration) the rest of the phase repeats.

## Progress — 13.1 (Hashes & MAC)
- **SHA-256** — C runtime (`teko_crypto_sha2.c`) + FIPS 180-4 KATs (incl. 1M-byte vector,
  streaming-vs-one-shot). ✅
- **SHA-512 / SHA-384** — C runtime (`teko_crypto_sha512.c`) + FIPS KATs. ✅
- **HMAC-SHA-256 / -512** — C runtime (`teko_crypto_hmac.c`) + RFC 4231 KATs. ✅
- **Language wiring (interleaved, owner-requested early):** `hash.sha256(x)` compiles from
  **real `.tks`** through the `teko` binary to WASM and runs. It lexes as one dotted
  identifier (like `base64.encode`), lowers to `OP_CALL_RUNTIME` id 4, and the WASM backend
  emits an **in-module SHA-256** (`emit_wasm_hash_runtime` in `emit_wasm.c`) — no host
  crypto, no external deps; the produced module computes the digest itself, mirroring the
  KAT-tested C reference. Proven by `runtime/wasm/run-hash.mjs` (FIPS 180-4 vectors for
  `"abc"`, `""`, and the fox sentence) in `wasm.yml`, plus the IL/emit golden
  `test_frontend_interop_hash_sha256`. A `uses_hash` flag gates the runtime so non-hashing
  modules stay lean (the shared `$teko_strlen`/`$teko_hexc` helpers were factored into a
  common block). ✅
- **SHA-3 / SHAKE** — C runtime (`teko_crypto_sha3.c`): Keccak-f[1600] sponge, SHA3-256/512
  + SHAKE128/256 XOFs, FIPS 202 KATs (+ chunked-absorb / byte-squeeze equivalence). ✅
- **BLAKE3** — C runtime (`teko_crypto_blake3.c`): chunk/parent tree + CV stack, official
  test vectors (0/1/3/64/1024/2048/3072 — multi-chunk trees) + streaming equivalence. ✅

**13.1 status: hash + MAC primitives complete** (SHA-256, SHA-512/384, SHA-3/SHAKE, BLAKE3,
HMAC), each KAT-tested in C, with the `hash.sha256` language surface proven executable from
real `.tks`. Deferred to the start of the next sub-phase (not blocking 13.1 close): wiring
`hash.sha512` / `hash.sha3_256` / `hash.blake3` to the WASM backend (the C runtimes are the
source of truth; only `sha256` has its WASM lowering so far). Suite 109/109; all four CI
gates green.

## Progress — 13.3a (CSPRNG + cheap KDF)
- **Platform CSPRNG** — C runtime (`teko_crypto_random.c`): `BCryptGenRandom` (Windows,
  links `bcrypt`), `arc4random_buf` (macOS/BSD), `getrandom` + `/dev/urandom` fallback
  (Linux/POSIX). No KAT possible (non-deterministic); wrapper-contract + distribution
  sanity tests instead. ✅
- **HKDF-SHA256/512** — C runtime (`teko_crypto_hkdf.c`), RFC 5869 SHA-256 KATs (cases
  1–3 incl. zero-salt default) + over-long rejection; SHA-512 structural invariant. ✅
- **PBKDF2-HMAC-SHA256/512** — C runtime (`teko_crypto_pbkdf2.c`), RFC 7914 §11 SHA-256
  KATs (c=1 and c=80000) + zero-iteration rejection; SHA-512 structural invariant.
  (RFC 6070 is HMAC-SHA1-only, which Teko deliberately omits → RFC 7914 is the
  authoritative SHA-256 source.) ✅

**13.3a status: complete.** Suite 115/115; all four CI gates green.

### Decision (registered): deferred WASM crypto lowering
The C runtime is the source of truth for every primitive. Only `hash.sha256` has a WASM
lowering so far (hand-emitted, FIPS-validated). Hand-emitting SHA-512 (i64), SHA-3 (Keccak)
and BLAKE3 in WAT is **rejected** — large, fragile, and a second implementation of each
algorithm, contradicting "implement once in C." The right WASM path is to **compile the C
crypto runtime to wasm32 and import it** (the same host-import shape as `dom.*`), which also
covers the WASM **CSPRNG host import**. That is its own infrastructure increment, sequenced
as a dedicated "crypto → wasm" step (candidate: end of 13.2 or a standalone increment), not
hand-emission. Until then `hash.sha512`/`sha3`/`blake3` and `random`/HKDF/PBKDF2 remain
**reserved-with-target** on the WASM surface (no dead tokens: they are not exposed in `.tks`
for the WASM target yet; the native C runtimes are fully KAT-tested).

## Next: 13.2 — Symmetric / AEAD
ChaCha20-Poly1305 (RFC 8439) first (no hardware dep), then AES-128/192/256 CTR/CBC/GCM.
**Owner gate before AES:** confirm the constant-time AES strategy (bitsliced vs.
table-free). The RSA bignum layer remains a later gate (before 13.3b RSA).
