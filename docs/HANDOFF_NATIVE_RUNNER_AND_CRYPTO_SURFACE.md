# Hand-off — Native Runner + Full Crypto Language Surface (then WASM follow-ups)

## ⇒ Sub-phase C "big step" — compile the C crypto runtime → wasm32
**STATUS: ✅ DONE & CI-green (2026-06-15).** Phase 13 is now FULLY COMPLETE — runtime + native
surface + WASM surface. The full crypto language surface (hashes/HMAC/AEAD/KDF/signatures/RSA,
OP_CALL_RUNTIME ids 5,10-40) works on the WASM target via the compiled-C reactor; no id traps any
more. See the "Sub-phase C, step 3 (big step)" progress-log entry below for the mechanism and
inventory. The original cold-start brief that follows is retained for historical context.

### (historical cold-start brief — superseded by the DONE entry above)
This was the one substantial remaining piece; everything else
was already DONE and CI-green. It was handed off because it is a multi-increment, toolchain-heavy
sub-project that **cannot be validated on the macOS dev box without installing a wasm32 toolchain**
(see the toolchain box below — Apple clang lacks the wasm32 target), so it warrants a fresh,
well-resourced session rather than low-context blind-on-CI iteration. State at hand-off (HEAD
`fed2b2d`): native runner + full native crypto surface DONE (A.1, B.1–B.10); WASM `random.bytes`
(C.1) and `uuid.v4/v7` (C.2) DONE via host imports. Branch `feat/phase-13-native-crypto` (PR #6),
single owner, tree clean, all 4 gates green.

**The remaining problem:** every other crypto runtime id (4-40 — hashes beyond sha256, HMAC,
AEAD, Ed25519/X25519, ECDSA, RSA) still emits `unreachable` on the WASM target
(`emit_wasm.c` `OP_CALL_RUNTIME` default = reserved-with-target). The decision (CLAUDE.md) is
**NOT** to hand-emit WAT per primitive (that's a second implementation of each algorithm) — instead
**compile the single C runtime (`src/runtime/teko_crypto_*.c`) to wasm32 and have the emitted
module use it**, so WASM lowers to the SAME source of truth as native.

**⚠️ TOOLCHAIN — verified findings (2026-06-15, save the next session a dead end):**
- The macOS **Apple `clang` (`/usr/bin/clang`) has NO wasm32 target** ("No available targets …
  compatible with triple wasm32"). Use **brew LLVM** instead: `/opt/homebrew/opt/llvm/bin/clang`
  *does* list `wasm32`. (Linux CI's `clang` may or may not — verify `clang --print-targets | grep
  wasm`; install `lld` + a wasm-capable clang if not.)
- **No `wasm-ld`** in the brew llvm bin by default → `brew install lld` (provides
  `wasm-ld`); the Linux CI job needs `apt-get install -y lld` (or the wasi-sdk's bundled one).
- **Freestanding has NO `<string.h>`/`<stdlib.h>`** — the crypto sources `#include <string.h>`
  (memcpy/memset/strlen) and some use `<stdlib.h>` (malloc). Two clean options:
  - **Recommended: wasi-sdk** (`wasm32-wasi` clang + a real libc → string.h/stdlib.h/malloc all
    present, no shims). Download the release tarball (locally + a CI install step). Heavier but
    removes ALL the shim yak-shaving; compile with `--target=wasm32-wasi --sysroot=<wasi-sdk>/share/wasi-sysroot`.
  - **Lighter: brew llvm + lld + hand shims** — provide a minimal `string.h`/`stdlib.h` on the
    include path and freestanding `memcpy/memset/memmove` definitions (the compiler also lowers
    struct copies to `memcpy`/`memset` libcalls — must be defined). malloc → route to `$teko_alloc`.
  Decide ONE up front; don't mix.
- A first end-to-end smoke (do this BEFORE touching the emitter): compile ONE pure hash
  (`teko_crypto_sha512.c`, no malloc/entropy) → a reactor `.wasm`
  (`-Wl,--no-entry -Wl,--export=teko_sha512 -Wl,--import-memory`), instantiate in Node against a
  shared `WebAssembly.Memory`, and check a known digest. Only once that works, wire the glue+emitter.

**Recommended approach (investigate + decide first — this is a design step):**
1. **Build the crypto C → wasm32** (see toolchain box above). Start with the pure hashes (no
   malloc/entropy) to prove the pipeline, then the rest. `TEKO_CRYPTO_SOURCES` is already a CMake
   var. CSPRNG entropy → the `env.teko_random` host import already wired (C.1); the wasm32 build
   of `teko_crypto_random.c` will need its OS-entropy `#if`s routed to that import (it currently
   targets getrandom/arc4random/BCrypt — none exist on wasm32).
2. **Link/compose with the emitted module.** Two options to evaluate: (a) `wasm-ld` the crypto
   `.o` set + the teko-emitted module's `.o` into one module (requires emitting the teko module
   as an object, not just WAT — bigger change); or (b) keep the crypto as a SECOND wasm module
   imported by the teko module (multi-module instantiation in the harness) — simpler to wire,
   needs a shared `memory`. **(b) is likely the smaller first step**; the hex-at-surface ABI means
   the boundary is just `(ptr,len) -> (ptr)` string calls.
3. **ABI on WASM = same hex-at-surface.** Reuse `teko_rt_*`-shaped entry points (they already take
   `const char*` hex and return `char*` hex). Compile thin `teko_rt_*` wrappers to wasm32 too, so
   the WASM dispatch becomes `call $teko_rt_<id>` exactly like native — then `emit_wasm.c`
   `OP_CALL_RUNTIME` maps ids 4-40 to those imported symbols (mirror `teko_native_runtime_symbol`).
4. **Multi-arg on WASM.** Declare the `$a0..$aN` staging locals and have the WASM `OP_CALL_RUNTIME`
   marshal staging slots → call args (mirror what `emit_native_hosted`/`emit_call` do; today the
   WASM dispatch only passes `$w0`). Needed for HMAC/AEAD/sign/verify (arity 2-4).
5. **Proofs.** One executable `.tks` per group under Node (reuse the B.* KAT vectors), wired into
   `wasm.yml`. Same hex inputs/outputs as the native proofs → copy the expected strings.
   Increment per group (hash family, HMAC, AEAD, signatures, RSA), 4 gates green each.

**Pitfalls:** the teko module's bump allocator (`$teko_alloc`, range [16384..65536)) vs the crypto
C's `malloc` must share ONE linear memory and not collide — decide the heap model up front. RSA
needs sizable scratch (2048-bit bignums) — ensure the heap is big enough (grow `(memory N)`).
Keep every op stack-neutral (accumulator model) or the module won't instantiate.

**Key files:** `src/codegen/bare_metal/emit_wasm.c` (`OP_CALL_RUNTIME` dispatch ~L1117, the
`unreachable` default; `emit_wasm_imports`; the runtime emitters), `src/codegen_li_wasm.c` (bridge
+ the `teko_metal_set_emit_*` calls), `CMakeLists.txt` (`TEKO_CRYPTO_SOURCES`), `runtime/wasm/run-*.mjs`
(harness pattern — see `run-uuid-rng.mjs`/`run-random.mjs` for the host-import shape), `wasm.yml`.
The native dispatch table `teko_native_runtime_symbol` (`src/codegen/emit_native_hosted.c`) is the
id→symbol map to mirror. The native `teko_rt_*` wrappers (`runtime/native/teko_rt.c`) are the exact
ABI to recompile for wasm32.

---

## Progress log (update as sub-phases land)
- **Sub-phase A, step 1 — native runner foundation: DONE.** A real `teko build
  <f>.tks --target=<native>` path now compiles real source through
  `teko_compile_interop` → IL → a **libc-hosted, assemble-able** emitter
  (`src/codegen/emit_native_hosted.c`, selected by `MetalContext.hosted`) → the system
  `cc` assembles + links against the new **`teko_rt`** archive (`runtime/native/teko_rt.c`,
  `libteko_rt.a`) → a **runnable** executable. Proven by `runtime/native/samples/hello.tks`:
  `emit("hello from teko native")` lowers to `OP_CALL_IMPORT` → SysV/AAPCS call into
  `teko_rt_emit` and prints natively on **macOS arm64** (local) and **Linux x86_64/arm64**
  (CI). Harness: `runtime/native/run-native.sh` (compile→link→run→assert), wired into
  `native.yml` for the Unix hosts. The 16 freestanding goldens are **untouched** (the
  hosted path is gated, not a rewrite of the metal emitters). Windows (PE-COFF + link.exe)
  native runner remains future work. **Next:** step 2 — native `OP_CALL_RUNTIME` lowering
  is already emitted by `emit_native_hosted` (dispatch table mirrors the WASM emitter); the
  remaining work is the `teko_rt_*` crypto wrappers + executable `.tks` KATs (`hash.sha256`
  first).
- **Sub-phase B, step 1 — `hash.sha256` native surface: DONE.** `hash.sha256(msg)` lowers
  to `OP_CALL_RUNTIME` id 4 → `teko_rt_sha256_hex` (in `libteko_rt.a`), which calls the
  portable C SHA-256 runtime. The crypto sources are now a shared CMake list
  (`TEKO_CRYPTO_SOURCES`) linked into BOTH `teko_core` and `teko_rt`, so produced binaries
  are self-contained. Proven by `runtime/native/samples/hash_sha256.tks`: `hash.sha256("abc")`
  prints the FIPS 180-4 digest natively on macOS arm64 + Linux x86_64/arm64 (asserted in
  `run-native.sh`).
- **Sub-phase B, step 2 — fixed-size hash family native surface: DONE.** `hash.sha384`,
  `hash.sha512`, `hash.sha3_256`, `hash.sha3_512`, `hash.blake3`, `hash.blake2b` (ids
  5/10/11/12/15/16) now lower to `teko_rt_*` wrappers over the C runtime. Proven by
  `runtime/native/samples/hash_family.tks` against FIPS/NIST/RFC `abc` vectors (BLAKE3 uses
  the spec empty-input vector — the official non-empty BLAKE3 vectors use a 0,1,2,… byte
  pattern, not ASCII). **No silent dead/wrong tokens on WASM:** the WASM `OP_CALL_RUNTIME`
  emitter now traps (`unreachable`) on a runtime id it doesn't yet lower (reserved-with-target)
  instead of mis-calling base64 — these ids get their real WASM lowering in Sub-phase C.
  **Next (B.3):** generalize the codec/runtime frontend lowering to **multi-arg** (it is
  single-arg today) — `emit_native_hosted` already marshals N args via OP_SETARG staging — to
  wire HMAC (key,msg), SHAKE (msg,len), then AEAD (`encrypt`/`decrypt`), `sign`/`verify`, RSA,
  ECDH, KDF/RNG.
- **Sub-phase B, step 3 — multi-arg runtime lowering + HMAC: DONE.** `lower_base_codec` now
  reads `runtime_arity(id)` args, staging args 0..n-2 via `OP_SETARG` and leaving the last in
  `$w0` (same convention as `OP_CALL_IMPORT`); nested codec args still work because staging is
  consumed synchronously at each call. First multi-arg surface: `hmac.sha256/384/512(hexKey,
  msg)` (ids 17/18/19, arity 2) → `teko_rt_hmac_*`, which hex-decodes the key (new
  `teko_rt_from_hex`) and MACs the message. Proven by `runtime/native/samples/hmac.tks`
  against RFC 4231 Test Case 2 on macOS arm64 + Linux x86_64/arm64. **This unblocks every
  remaining multi-arg surface.** ⚠️ Note for Sub-phase C: a multi-arg `OP_CALL_RUNTIME` on the
  *WASM* target currently emits `OP_SETARG` (→ `$a` locals) before an `unreachable` — fine as a
  reserved trap, but the WASM emitter must declare/handle those staging locals when it gains
  real multi-arg runtime lowering.
- **Sub-phase B, step 4 — AEAD `encrypt`/`decrypt` native surface: DONE.** `crypto.aes_gcm_seal/
  open` (ids 20/21) and `crypto.chacha20poly1305_seal/open` (22/23), all 4-arg
  (key,nonce,aad,msg‖ct‖tag — all hex). seal returns ct‖tag packed into one hex string
  (multi-value return per the ABI decision); open verifies the tag and returns the plaintext
  hex or the literal `"REJECT"` on auth failure / malformed input. Proven by
  `runtime/native/samples/aead.tks`: AES-GCM (NIST Test Case 3) + ChaCha20-Poly1305 (RFC 8439
  §2.8.2) seal→ct‖tag, open round-trip→pt, and a tamper→REJECT, on macOS arm64 + Linux
  x86_64/arm64. The 4-arg SysV/AAPCS staging is verified. AES key length inferred from the key
  (128/192/256); tag fixed 16 bytes.
- **Sub-phase B, step 5 — Ed25519 sign/verify native surface: DONE.** `crypto.ed25519_sign`
  (id 24, arity 2: seedHex, msgHex → sigHex; derives the pubkey internally) and
  `crypto.ed25519_verify` (id 25, arity 3: pubHex, msgHex, sigHex → `"1"`/`"0"`). Proven by
  `runtime/native/samples/sign_ed25519.tks` against RFC 8032 Test 3 (deterministic signature
  reproduced exactly; valid→1; tampered→0) on macOS arm64 + Linux x86_64/arm64.
- **Sub-phase B, step 6 — X25519 ECDH native surface: DONE.** `crypto.x25519(scalarHex, uHex)`
  (id 26, arity 2) → 32-byte shared-secret hex. Proven by `runtime/native/samples/x25519.tks`
  against RFC 7748 §5 vectors 1 & 2 on macOS arm64 + Linux x86_64/arm64.
- **Sub-phase B, step 7 — KDF native surface (HKDF + PBKDF2): DONE.** `kdf.hkdf_sha256(ikmHex,
  saltHex, infoHex, len)` (id 27) and `kdf.pbkdf2_sha256(passHex, saltHex, iters, len)` (id 28),
  both arity 4. **`lower_codec_value` now accepts integer-literal args** (ICONST immediates) for
  the length/iteration parameters — the hosted emitter marshals an int in $w0 the same as a
  pointer. Output capped at 1024 bytes. Proven by `runtime/native/samples/kdf.tks` against RFC
  5869 HKDF Test Case 1 and the suite's PBKDF2-HMAC-SHA256 vector, on macOS arm64 + Linux
  x86_64/arm64. (sha512 KDF variants are an easy follow-up: ids + wrappers, same pattern.)
- **Sub-phase B, step 8 — ECDSA P-256/P-384 sign/verify native surface: DONE.**
  `crypto.ecdsa_p256_sign/verify` (ids 29/30) and `crypto.ecdsa_p384_sign/verify` (31/32).
  sign(privHex, hashHex) → r‖s hex (RFC 6979 deterministic); verify(pubHex, hashHex, sigHex) →
  `"1"`/`"0"`. The hash is the message digest (SHA-256 / SHA-384). Proven by
  `runtime/native/samples/sign_ecdsa.tks` against RFC 6979 A.2.5 (P-256) and A.2.6 (P-384) for
  "sample": deterministic signature reproduced exactly, valid→1, tampered→0, on macOS arm64 +
  Linux x86_64/arm64.
- **Sub-phase B, step 9 — SHAKE + RSA native surface: DONE.** `hash.shake128/256(msg, outLen)`
  (ids 33/34, arity 2) → `outLen` squeezed bytes as hex (FIPS 202 empty-message KAT). RSA
  (ids 37-40, RFC 8017, SHA-256/MGF1): `crypto.rsa_pss_sign(n,d,mhash)` /
  `rsa_pss_verify(n,e,mhash,sig)` (random salt sLen=32) and `crypto.rsa_oaep_encrypt(n,e,msg)` /
  `rsa_oaep_decrypt(n,d,ct)` (random seed, empty label). The RSA surface is **secure-by-default**
  (randomized salt/seed → non-deterministic sig/ct), so `runtime/native/samples/rsa.tks` proves
  the deterministic outcomes — PSS sign→verify (1), wrong-message verify (0), OAEP
  encrypt→decrypt round-trip recovering the exact plaintext — with the 2048-bit key `let`-bound;
  the crypto math is KAT-pinned in the Unity suite. Proven on macOS arm64 + Linux x86_64/arm64.
- **Sub-phase B, step 10 — CSPRNG native surface: DONE.** `random.bytes(n)` (id 41, arity 1) →
  n cryptographically-secure random bytes as hex. Output is non-deterministic, so
  `runtime/native/samples/random.tks` + the harness `check_random` assert the format (two
  64-hex-char draws) and that the two draws differ, on macOS arm64 + Linux x86_64/arm64.
- **Sub-phase B — COMPLETE.** The full crypto **language surface** is now functional natively:
  hashes (sha256/384/512, sha3_256/512, shake128/256, blake3, blake2b, legacy md5/sha1), HMAC,
  AEAD (AES-GCM + ChaCha20-Poly1305), signatures (Ed25519, ECDSA P-256/384, RSA-PSS), RSA-OAEP,
  X25519 ECDH, KDF (HKDF/PBKDF2), CSPRNG, UUID — each lowering to the single C runtime via
  `OP_CALL_RUNTIME` → `teko_rt_*`, each with an executable `.tks` KAT in `runtime/native/run-native.sh`
  (run on macOS arm64 + Linux x86_64/arm64 in `native.yml`). No dead tokens on the native surface.
- **Sub-phase C, step 1 — WASM CSPRNG via host entropy import: DONE.** `random.bytes(n)` (id 41)
  now lowers on the WASM target to an in-module `$teko_random_hex` wrapper that allocates n bytes
  via `$teko_alloc`, calls a host import `env.teko_random(ptr, len)` for entropy (Node
  `crypto.randomFillSync` / browser `crypto.getRandomValues`), and hex-encodes in-module (no codec
  runtime dependency — gated by `uses_random` → `teko_metal_set_emit_random`). Proven by
  `runtime/wasm/samples/random.tks` + `runtime/wasm/run-random.mjs` (the host fills a deterministic
  counter pattern so the emitted hex is an exact KAT of the (ptr,len) ABI + in-module hex encode),
  wired into `wasm.yml`. (The id no longer traps on WASM; the C-runtime-→-wasm32 step below will
  give the *real* CSPRNG, but the host-import path is the correct entropy source on WASM regardless.)
- **Sub-phase C, step 2 — uuid.v4/v7 (native + WASM): DONE.** `uuid.v4()`/`uuid.v7()` (ids 42/43)
  now lower on BOTH targets. Native → `teko_rt_uuid_v4/v7` (C CSPRNG + host clock for v7).
  WASM → a self-contained in-module `$teko_uuid_v4/v7` runtime (own inline formatter, no
  codec/hash dependency) drawing entropy from `env.teko_random` and (v7) a 48-bit Unix-ms
  timestamp from a new `env.teko_now` (i64) host import — gated by a new `uses_uuid_rng` flag.
  The 0-arg surface lowers an ignored `$w0`. Proofs: `runtime/native/samples/uuid_rng.tks` +
  `check_uuid` (asserts canonical layout + version/variant nibbles) and
  `runtime/wasm/samples/uuid_rng.tks` + `run-uuid-rng.mjs` (host fills deterministic
  entropy/time → exact v4/v7 KATs). macOS arm64 + Linux x86_64/arm64 + Node.
- **Sub-phase C, step 3 — the "big step": full crypto surface on WASM via a compiled-C
  reactor: DONE (2026-06-15).** Every remaining crypto runtime id (5,10-40 — hashes beyond
  sha256, HMAC, AEAD, KDF, X25519, Ed25519, ECDSA, SHAKE, RSA) now lowers on the WASM target
  to the SAME single C runtime as native, NOT a hand-emitted WAT re-implementation. Mechanism:
  - **Reactor:** `src/runtime/teko_crypto_*.c` + the `teko_rt_*` hex-at-surface wrappers +
    `teko_uuid.c` are compiled to a freestanding **wasm32 "reactor" module** (`crypto.wasm`)
    by `runtime/wasm/crypto/build-crypto-reactor.sh` (a wasm32-capable clang + `wasm-ld` from
    LLVM lld + the dir's tiny libc shim — `memcpy/memset/memmove/memcmp/strlen` + a bump
    `malloc`; **no wasi-sdk**). `<stdint.h>`/`<stddef.h>` are clang freestanding headers; the
    shim supplies `<string.h>`/`<stdlib.h>`. `teko_crypto_random.c` routes its OS-entropy `#if`
    to a new `__wasm__` branch that calls the host import `env.teko_random` (RSA PSS/OAEP draw
    their salt/seed from it). `teko_rt.c`'s emit + uuid.v4/v7 + random tail (ids 41-43, already
    in-module on WASM) is guarded out under `#if !defined(__wasm__)` — single source preserved,
    native untouched.
  - **Shared memory, no allocator collision:** the reactor is linked
    `--no-stack-first --global-base=65536 -z stack-size=1MiB`, so its entire image (rodata +
    shadow stack) and its bump heap (from the linker `__heap_base`) live ABOVE Teko's
    `[0..65536)` region (string pool / arena / `$teko_alloc`). The reactor imports `env.memory`;
    the emitted Teko module ALSO imports `env.memory` (host-owned & shared) whenever a
    reactor-backed id is used — gated by a new `uses_crypto_ext` flag so every non-crypto
    program is byte-for-byte unchanged (verified by diff).
  - **Emitter:** `emit_wasm.c` declares `(import "crypto" "teko_rt_*" …)` per reactor id (arity
    from the shared `teko_native_runtime_symbol`), switches memory to imported, and routes
    `OP_CALL_RUNTIME` ids 5,10-40 to `call $crypto_<id>` with the existing `$a0..$a2` staging
    ABI (max arity 4). The `unreachable` trap remains only for genuinely-unwired ids (none
    remain in the surface — no dead tokens).
  - **Proofs:** `runtime/wasm/samples/crypto_{hash,hmac,aead,sign,kdf,rsa}.tks` compiled by the
    real `teko` binary → wat → wasm, instantiated against the reactor under Node by
    `runtime/wasm/run-crypto.mjs`, asserting the SAME FIPS/NIST/RFC KAT vectors as the native
    proofs (32 vectors total: hash 8, HMAC 3, AEAD 5, sign/ECDH 11, KDF 2, RSA 3). Wired into
    `wasm.yml` (`apt-get install lld`; build reactor; compile + assemble + run). All four gates
    green; 167/167 Unity suite, ASan/UBSan both dispatch paths + TSan clean; 16 native goldens
    and the native crypto runner unchanged.

> **Status:** owner-approved next work. Same PR/branch as Phase 13:
> `feat/phase-13-native-crypto` (PR #6). The Phase 13 crypto **runtime** is complete and
> CI-green (167/167, all four gates); this document is the cold-start brief for the *next*
> session, which the owner asked to run fresh and dedicated to the native backend.
>
> **Read first:** root `CLAUDE.md`, `src/CLAUDE.md`, `runtime/wasm/CLAUDE.md`,
> `.claude/agents/teko-engineer.md`, and `docs/PHASE13_NATIVE_CRYPTO.md`. This brief assumes
> that context and the non-negotiable discipline (1 increment/commit; build + ASan + UBSan on
> BOTH dispatch paths + TSan each commit; goldens not regressed; **executable** validation, not
> `strstr`; all four CI gates green before anything is "done"; **no `git merge` / no
> force-push** — the human merges).

## Owner's two-point directive (verbatim intent)
1. **NOW — wire the FULL crypto LANGUAGE SURFACE to the C runtime, no dead tokens.** The
   reserved tokens `crypto` / `encrypt` / `decrypt` / `sign` / `verify` (+ the missing
   `hash.*`) must become functional surface lowering to the native C runtime via
   `OP_CALL_RUNTIME` (the mechanism used for `hash.sha256` / `uuid.v3`), with an **executable
   `.tks` proof** compiled by the real `teko` binary. Cover coherently:
   - `hash.*` still missing surface: sha384/sha512, sha3/shake, blake3, blake2b.
   - HMAC.
   - Symmetric/AEAD: `encrypt`/`decrypt` → ChaCha20-Poly1305 and AES-GCM (key/nonce/aad →
     ciphertext+tag and the inverse, with tag rejection).
   - Asymmetric: `sign`/`verify` → Ed25519, ECDSA P-256/384, RSA-PSS; asymmetric
     encrypt/decrypt → RSA-OAEP; key exchange X25519/ECDH if it fits the surface.
   - KDF/RNG on the surface if it fits (HKDF/PBKDF2; random).
   Design a clean, consistent namespace/grammar and document it.
2. **AFTER — WASM follow-ups, in order:** (a) host import for entropy/time → unlocks
   `uuid.v4`/`v7` + CSPRNG on the WASM surface; (b) compile the C crypto runtime → wasm32 +
   import → unlocks everything else (hash/ciphers/asymmetric) on the WASM surface. Executable
   proofs in the Node/Chromium harness.

All in the **same PR #6**. Pre-approved; decide the design and proceed; report at block closes.

## Two design decisions the owner already made (this session)
- **Substrate = native-first.** Lower the surface to the linked **native** C runtime via
  `OP_CALL_RUNTIME` + `tld`. (NOT hand-emitted WAT per primitive — that contradicts the
  standing architecture decision and is infeasible for RSA/ECDSA/AEAD. WASM comes in point 2.)
- **Binary ABI = hex at surface, bytes internally.** Surface fields that are binary
  (keys, nonces, aad, ciphertext, tags, signatures, pubkeys) are **hex string literals** at
  the `.tks` level, decoded to length-carrying byte buffers internally; multi-value returns
  (e.g. ciphertext‖tag) are packed into one buffer (hex out). This reuses the already-wired
  base64/hex codecs and lets KATs assert exact bytes without a new first-class buffer type.

## ⚠️ The blocking reality the next session must internalize first
Point 1 cannot be an increment on top of what exists, because **the native backend is
emission-only ("no runner")** — confirmed by reading the code, and stated in `README.md`
("🟡 Emission only — no runner"):

- **The native build compiles MOCK bytecode, not real `.tks`.** `src/main.c` (~L186–251):
  every `--target=<native>` build encodes a hardcoded `mock_bytecode[] = {0x01,0x1F,…}`
  (`ICONST 31; HALT`) through `tld_arch_encode_instruction` (an `ICONST`/`HALT`-level encoder)
  → `tld_elf64_write_executable`. Only `--target=wasm` runs the real frontend
  (`teko_compile_interop` → IL → `.wat`).
- **Emitter cores emit assembly *text*, goldens-only.** `src/codegen/emit_x86_64_sysv_common.c`,
  `emit_arm64_gas_common.c`, etc. `fprintf` a toy 2-register accumulator model (`eax`/`ebx`);
  the runtime-ish ops are symbolic stubs (`OP_SPAWN_ASYNC` → bare `call pthread_create` with no
  ABI; `OP_CHAN_*` just bump a register). Tests (`tests/codegen/test_codegen_*.c`) `strstr` the
  emitted text. **Nothing assembles or runs native output.**
- **No external runtime linking.** `tld_symbols.c` resolves only intra-program symbols; no code
  links `teko_crypto_*.o` (or `teko_thread.o`) into produced binaries. The design-doc line
  claiming the runtime is "linked into produced native binaries via tld, exactly as the
  concurrency runtime is" was **aspirational/inaccurate** (now corrected in
  `docs/PHASE13_NATIVE_CRYPTO.md` and `CLAUDE.md`).
- `OP_CALL_RUNTIME` (`0x19`, `src/codegen_li.h`) is handled **only** by the WASM emitter
  (`emit_wasm.c`), where `hash.sha256` etc. are in-module hand-emitted WAT — there is **no
  native `OP_CALL_RUNTIME` lowering at all.**

**So Sub-phase A below (build a real native runner) is a prerequisite for the surface, and is
the single largest piece of work in the compiler.**

## Recommended approach (pragmatic — reuse the system toolchain, don't write an assembler)

### Sub-phase A — Native runner foundation (host arch first)
Goal: a real `.tks` compiles to a **runnable** native binary that can call into the C runtime
and print output. Do **one host arch first** (Linux x86_64 — the CI native runner; then macOS
arm64 which is also a CI runner; then QEMU for the rest, non-blocking like riscv today).
1. **Route real IL to native targets.** In `src/main.c`, for native targets run
   `teko_compile_interop` (or the broader real frontend) to a `BytecodeBuffer` and feed THAT to
   the emitter (`teko_metal_emit_program`) instead of `mock_bytecode`. (WASM already does this
   via `codegen_li_emit_wasm`.)
2. **Make the emitter cores emit ABI-correct, assemble-able code** for the needed opcode
   subset, libc-hosted (crypto runtime needs libc: memcpy/malloc/getrandom):
   - `OP_PROLOG`/`OP_HALT` → a real `main` that returns `int` to libc crt0 (not a raw syscall
     exit) when linking hosted.
   - `OP_SCONST` → emit the string bytes in `.rodata` (a `strN: .asciz "…"`) + a rip-relative
     `lea`. (Today only the `lea` is emitted; verify whether the data section exists.)
   - `OP_CALL_IMPORT` → SysV/AAPCS call to an imported symbol (e.g. `teko_rt_emit`).
   - **New native `OP_CALL_RUNTIME` lowering** → arg pointer in `rdi`/`x0`, `call teko_rt_<id>`,
     result pointer back in the accumulator. Mirror the WASM dispatch table in `emit_wasm.c`
     (ids: 0–3 base64/hex, 4 sha256, 6 md5, 7 sha1, 8 uuid.v3, 9 uuid.v5; extend from there).
   - Update the goldens **intentionally** (they pin intended output; this is an intended
     improvement, not a regression — re-verify byte-identical shared cores across arches).
3. **`teko_rt` C archive.** A small library exposing the surface ABI over the existing
   `teko_crypto_*` runtime: `teko_rt_emit(const char*)` (puts), and `teko_rt_<prim>(...)`
   wrappers (hash → hex string; AEAD/sign/etc. taking/returning hex per the ABI decision).
   Build it as a static lib alongside `teko_core`.
4. **Drive the system assembler/linker.** Have `teko build --target=<native>` (or the test
   harness) assemble the emitted `.s` and link `teko_rt` via the system `cc` to produce a
   runnable executable. (Avoids writing a real instruction encoder + linker — `tld`'s
   `ICONST`/`HALT` encoder is not a basis for the full ISA.)
5. **Compile→link→run→assert harness in CI.** New harness (shell or a small driver) that
   compiles a `.tks`, links, runs, and asserts stdout — host arch in `native.yml` (QEMU for
   cross, non-blocking). First milestone proof: `emit("…")` round-trips a literal natively.

### Sub-phase B — Native crypto surface (no dead tokens)
On top of A, with the hex-at-surface ABI:
1. **Namespace/grammar (proposal — refine & document).** Reuse the dotted-identifier
   recognition in `frontend_interop.c` (`codec_id_for` @ ~L316, `lower_base_codec` @ ~L364) and
   `codegen_li_emit_call_runtime` (`codegen_li.c` ~L139). Extend `codec_id_for` with new ids
   and add multi-arg lowering (the current path is single-arg; AEAD/sign need 2–4 hex args —
   stage via `OP_SETARG` like `lower_call`/`lower_intrinsic_call` already do).
   Proposed surface (clean, consistent):
   - Hashes: `hash.sha384/sha512/sha3_256/sha3_512/shake128/shake256/blake3/blake2b(msg)`.
   - MAC: `hmac.sha256(keyHex, msg)` (and sha384/sha512).
   - AEAD: `crypto.aes_gcm_seal(keyHex, nonceHex, aadHex, msg) -> (ct‖tag)Hex`;
     `crypto.aes_gcm_open(keyHex, nonceHex, aadHex, ctTagHex) -> msg | <reject>`; same for
     `crypto.chacha20poly1305_seal/open`.
   - Signatures: `sign(alg, privHex, msgHex) -> sigHex` / `verify(alg, pubHex, msgHex, sigHex)
     -> 0|1`, `alg ∈ {ed25519, ecdsa_p256, ecdsa_p384, rsa_pss}`. (Or `crypto.ed25519_sign`
     etc. — pick one shape and be consistent; the owner gave both as examples.)
   - Asymmetric enc: `crypto.rsa_oaep_encrypt(pubHex, msgHex) -> ctHex` / `_decrypt(privHex,
     ctHex) -> msgHex`.
   - KEX: `crypto.x25519(privHex, peerPubHex)`, `crypto.ecdh_p256/p384(privHex, peerPubHex)`.
   - KDF/RNG: `kdf.hkdf_sha256(ikmHex, saltHex, infoHex, len)`,
     `kdf.pbkdf2_sha256(passHex, saltHex, iters, len)`, `random.bytes(n) -> hex`.
2. **Per surface, one increment:** id + `teko_rt_*` wrapper + native lowering + an executable
   `.tks` KAT (compile via `teko`, link, run, assert against the SAME NIST/RFC/Wycheproof
   vectors already used by the C-runtime KATs). **No dead tokens** — every exposed op does real
   work with a passing executable proof.
3. Decode hex args in the `teko_rt_*` wrappers (reuse `teko_hex_*`); pack multi-returns; for
   `*_open`/`verify` map AEAD-tag/signature failure to a clear surface result.

### Sub-phase C — WASM follow-ups (owner point 2, AFTER A+B)
- (a) **Host entropy/time import** (`env.teko_random`/`env.teko_now`) → wire `uuid.v4`/`v7` +
  `random` on the WASM surface; executable `.tks` proof in `run-*.mjs`.
- (b) **Compile `teko_crypto_*.c` → wasm32 + import** (same host-import shape as `dom.*`) so the
  whole surface lowers to the one C impl on WASM too; executable proofs in Node/Chromium.

## What is already DONE (do NOT redo) — the C runtime (source of truth)
All KAT-tested in the Unity suite (`tests/runtime/test_crypto_*.c`), 167/167, four gates green:
- Hashes/MAC: SHA-256/384/512, SHA-3/SHAKE, BLAKE3, BLAKE2b, HMAC-SHA-256/384/512.
- KDF/RNG: CSPRNG (`teko_csprng_bytes`), HKDF, PBKDF2, scrypt, Argon2 d/i/id.
- Symmetric/AEAD: ChaCha20-Poly1305, AES-128/192/256 CTR/CBC/GCM (constant-time).
- Asymmetric: X25519, Ed25519, P-256 & P-384 ECDH/ECDSA (`teko_crypto_ec/ecc/p256/p384.c`),
  RSA PKCS#1 v1.5 (sign+encrypt)/OAEP/PSS (`teko_crypto_rsa.c`) on the Montgomery bignum
  (`teko_crypto_bn.c`).
- UUID nil/v3/v4/v5/v7/v8 + parse/format.
These are the functions the `teko_rt_*` wrappers call. The corresponding KAT vectors in the
test files are ready to reuse as the executable-`.tks` oracles.

## Current language surface today (the only non-dead tokens)
WASM-only, in-module hand-emitted WAT, proven by `run-hash.mjs` / `run-uuid.mjs`:
`hash.sha256`, `hash.md5`, `hash.sha1`, `uuid.v3`, `uuid.v5`. Everything else is reserved
(`crypto`/`encrypt`/`decrypt`/`sign`/`verify`, the other `hash.*`) — runtime exists, surface
does not. Sub-phase B makes them functional.

## Key files / extension points (file:line)
- Frontend surface recognition: `src/frontend_interop.c` — `codec_id_for` ~L316,
  `lower_base_codec` ~L364, `lower_call` (multi-arg staging) ~L90, `lower_intrinsic_call` ~L146.
- IL op: `src/codegen_li.h` `OP_CALL_RUNTIME = 0x19`; `src/codegen_li.c`
  `codegen_li_emit_call_runtime` ~L139 (sets `uses_hash`/`uses_codec`).
- WASM dispatch (template): `src/codegen/bare_metal/emit_wasm.c` OP_CALL_RUNTIME ~L1071, runtime
  emitters `emit_wasm_hash_runtime`/`_md5_`/`_sha1_`/`_uuid_`.
- Native main path (mock bytecode to replace): `src/main.c` ~L186–251.
- Native emitter cores (to make real): `src/codegen/emit_x86_64_sysv_common.c`,
  `emit_arm64_gas_common.c`, `linux/emit_linux_riscv_common.c`; router `codegen_metal.c`.
- Object/linker: `src/codegen/tld_*.c` (`tld_arch_encode_instruction`,
  `tld_elf64_write_executable`, `tld_symbols.c`).
- WASM proof harness (template for a native one): `runtime/wasm/run-hash.mjs`,
  `runtime/wasm/samples/hash.tks`.
- CI: `.github/workflows/native.yml` (add a compile→link→run→assert step + a `gate`-aggregated
  job; never `on: paths:` a required workflow).

## Suggested first three commits (each green, each runnable)
1. `teko_rt` C archive + route real IL to ONE native target in `main.c` + emit a libc-hosted
   `main` + `OP_CALL_IMPORT` for `teko_rt_emit`; prove `emit("hello")` compiles→links→runs→prints
   natively (new CI harness). Goldens updated intentionally.
2. Native `OP_CALL_RUNTIME` lowering + `teko_rt_sha256_hex`; prove
   `emit(hash.sha256("abc"))` runs natively and prints the FIPS digest (assert in CI).
3. Generalize to the remaining `hash.*` + HMAC (single/two hex args); then AEAD, then
   sign/verify, then RSA — one surface per commit, each with an executable `.tks` KAT.

Report at the close of: Sub-phase A (runner), Sub-phase B (full surface, no dead tokens), then
Sub-phase C (WASM). Same PR #6; the human merges.
