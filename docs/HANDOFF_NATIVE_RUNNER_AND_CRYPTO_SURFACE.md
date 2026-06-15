# Hand-off — Native Runner + Full Crypto Language Surface (then WASM follow-ups)

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
- **Sub-phase B — REMAINING:** SHAKE (msg,len), ECDSA P-256/384 sign/verify, RSA-PSS
  sign/verify, RSA-OAEP encrypt/decrypt, KDF (HKDF/PBKDF2), RNG (`random.bytes`).
  Each: `codec_id_for` id + `runtime_arity` + `teko_native_runtime_symbol` entry + `teko_rt_*`
  wrapper (hex-at-surface) + an executable `.tks` KAT in `run-native.sh`. The established
  pattern (see AEAD/HMAC/Ed25519) scales directly; 8 staging slots cover all current arities.
  For ECDSA, check the `teko_crypto_ecc/p256/p384` APIs for the key/point + nonce (RFC 6979)
  shapes; for RSA, `teko_crypto_rsa` for the key encoding the KATs expect.

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
