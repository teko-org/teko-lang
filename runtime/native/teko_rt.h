#ifndef TEKO_RT_H
#define TEKO_RT_H

// Teko native runtime ABI (Phase 13 — native runner / crypto language surface).
//
// This is the thin C shim that a `teko build --target=<native>` executable links
// against. The hosted native emitter (src/codegen/emit_native_hosted.c) lowers the
// IL opcodes OP_CALL_IMPORT / OP_CALL_RUNTIME to SysV/AAPCS calls into these
// symbols. It is the native counterpart of the in-module WASM runtime: the SAME
// portable C crypto runtime (src/runtime/teko_crypto_*.c) is the single source of
// truth — these wrappers only adapt the hex-at-surface ABI (see the hand-off doc
// docs/HANDOFF_NATIVE_RUNNER_AND_CRYPTO_SURFACE.md) onto it.
//
// ABI: surface values that are binary (keys/nonces/ciphertext/tags/signatures) are
// hex strings at the .tks level and arrive here as NUL-terminated `const char*`;
// results are NUL-terminated heap strings the wrapper allocates. Plain text/print
// values pass through as NUL-terminated C strings.

// The print primitive: `extern fn emit(s) from "teko_rt" as "teko_rt_emit"` lowers a
// top-level `emit("…")` to this. Writes the string followed by a newline to stdout.
void teko_rt_emit(const char* s);

// Hash surface (OP_CALL_RUNTIME id 4). `hash.sha256(msg)` hashes the raw bytes of the
// NUL-terminated message and returns a freshly-allocated lowercase hex digest string
// (caller-owned; short-lived programs leak it like the WASM bump allocator does). This
// matches the existing WASM hash surface (hash.sha256("abc") hashes the bytes "abc").
char* teko_rt_sha256_hex(const char* msg);

// Rest of the fixed-size hash family (OP_CALL_RUNTIME ids 5,10,11,12,15,16). Same shape
// as sha256: hash the raw message bytes, return a fresh lowercase hex digest string.
char* teko_rt_sha384_hex(const char* msg);   // id 10 — SHA-384
char* teko_rt_sha512_hex(const char* msg);   // id 5  — SHA-512
char* teko_rt_sha3_256_hex(const char* msg); // id 11 — SHA3-256
char* teko_rt_sha3_512_hex(const char* msg); // id 12 — SHA3-512
char* teko_rt_blake3_hex(const char* msg);   // id 15 — BLAKE3 (32-byte default output)
char* teko_rt_blake2b_hex(const char* msg);  // id 16 — BLAKE2b (64-byte default output)

#endif // TEKO_RT_H
