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

// Print an integer followed by a newline (Phase 14 — concurrency proofs surface i32 results
// such as channel values and structured statuses). `extern fn emit_int(n) … as "teko_rt_emit_int"`.
void teko_rt_emit_int(long n);

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

// HMAC (OP_CALL_RUNTIME ids 17/18/19, arity 2). Per the hex-at-surface ABI the key is a
// hex string (decoded to bytes here); the message is raw bytes. Returns a fresh lowercase
// hex MAC string, or NULL on a malformed key.
char* teko_rt_hmac_sha256(const char* key_hex, const char* msg);
char* teko_rt_hmac_sha384(const char* key_hex, const char* msg);
char* teko_rt_hmac_sha512(const char* key_hex, const char* msg);

// AEAD (OP_CALL_RUNTIME ids 20-23, arity 4). All inputs are hex (key/nonce/aad/plaintext or
// cipher‖tag). seal returns the ciphertext concatenated with the 16-byte tag, as one hex
// string (the multi-value return packed per the ABI decision). open verifies the tag and
// returns the plaintext hex on success, or the literal "REJECT" on authentication failure
// or malformed input. AES key length (16/24/32) is inferred from the key; ChaCha needs a
// 32-byte key + 12-byte nonce.
char* teko_rt_aes_gcm_seal(const char* key_hex, const char* nonce_hex,
                           const char* aad_hex, const char* pt_hex);
char* teko_rt_aes_gcm_open(const char* key_hex, const char* nonce_hex,
                           const char* aad_hex, const char* ct_tag_hex);
char* teko_rt_chacha20poly1305_seal(const char* key_hex, const char* nonce_hex,
                                    const char* aad_hex, const char* pt_hex);
char* teko_rt_chacha20poly1305_open(const char* key_hex, const char* nonce_hex,
                                    const char* aad_hex, const char* ct_tag_hex);

// Signatures — Ed25519 (OP_CALL_RUNTIME ids 24/25). sign takes a 32-byte hex seed and a hex
// message, derives the public key, and returns the 64-byte signature as hex. verify takes a
// 32-byte hex public key, hex message, and 64-byte hex signature; returns "1" if valid, "0"
// otherwise (or on malformed input).
char* teko_rt_ed25519_sign(const char* seed_hex, const char* msg_hex);
char* teko_rt_ed25519_verify(const char* pub_hex, const char* msg_hex, const char* sig_hex);

// Key exchange — X25519 (OP_CALL_RUNTIME id 26). x25519(scalarHex, uHex) -> 32-byte shared
// secret as hex. Both inputs must be 32 bytes; returns NULL on a malformed length.
char* teko_rt_x25519(const char* scalar_hex, const char* u_hex);

// KDF — HKDF-SHA-256 / PBKDF2-HMAC-SHA-256 (ids 27/28). Hex inputs; the last one/two args
// are integers (output length, and for PBKDF2 the iteration count). Returns the derived key
// as hex, or NULL on malformed input or an out-of-range length (capped at 1024 bytes).
char* teko_rt_hkdf_sha256(const char* ikm_hex, const char* salt_hex,
                          const char* info_hex, int out_len);
char* teko_rt_pbkdf2_sha256(const char* pass_hex, const char* salt_hex,
                            int iterations, int dk_len);

// SHAKE128/256 extendable-output functions (ids 33/34). msg is a plain string; out_len is the
// requested squeeze length in bytes; returns that many output bytes as lowercase hex.
char* teko_rt_shake128(const char* msg, int out_len);
char* teko_rt_shake256(const char* msg, int out_len);

// ECDSA over NIST P-256 / P-384 (ids 29-32, RFC 6979 deterministic). sign takes the private
// scalar (32/48 bytes) + a message-digest hash, returns the r‖s signature (64/96 bytes) as
// hex. verify takes the uncompressed public key (X‖Y, 64/96 bytes), the hash, and the
// signature; returns "1" if valid, "0" otherwise.
char* teko_rt_ecdsa_p256_sign(const char* priv_hex, const char* hash_hex);
char* teko_rt_ecdsa_p256_verify(const char* pub_hex, const char* hash_hex, const char* sig_hex);
char* teko_rt_ecdsa_p384_sign(const char* priv_hex, const char* hash_hex);
char* teko_rt_ecdsa_p384_verify(const char* pub_hex, const char* hash_hex, const char* sig_hex);

// RSA over SHA-256 + MGF1-SHA-256 (ids 37-40). Keys are big-endian hex. PSS uses a random
// salt of hLen (32) — sign returns the signature hex, verify returns "1"/"0". OAEP uses a
// random seed + empty label — encrypt returns ciphertext hex, decrypt returns the recovered
// message hex or "REJECT". The mhash arg to PSS is the 32-byte message digest.
char* teko_rt_rsa_pss_sign(const char* n_hex, const char* d_hex, const char* mhash_hex);
char* teko_rt_rsa_pss_verify(const char* n_hex, const char* e_hex,
                             const char* mhash_hex, const char* sig_hex);
char* teko_rt_rsa_oaep_encrypt(const char* n_hex, const char* e_hex, const char* msg_hex);
char* teko_rt_rsa_oaep_decrypt(const char* n_hex, const char* d_hex, const char* ct_hex);

// CSPRNG (id 41). random.bytes(n) -> n cryptographically-secure random bytes as hex.
// Returns NULL on n <= 0 or n > 1024.
char* teko_rt_random_bytes(int n);

// UUID v4 (random) / v7 (time-ordered + random) (ids 42/43). No surface args (the `int`
// parameter is the lowered, ignored accumulator). Returns a fresh canonical lowercase UUID
// string ("8-4-4-4-12"), or NULL if entropy is unavailable. v7 timestamps with the host's
// current Unix-ms clock.
char* teko_rt_uuid_v4(int ignored);
char* teko_rt_uuid_v7(int ignored);

// Phase 14 (14.A) — cooperative scheduler for `routines` (background tasks). Implemented in
// the separate TU teko_rt_sched.c (linked only when a binary uses routines). teko_rt_spawn
// enqueues the routine at function-table `slot` with `arg` (lowered from OP_SPAWN_ASYNC);
// teko_rt_run drains the queue to completion (called at `$main` exit). See teko_rt_sched.c.
void teko_rt_spawn(long slot, long arg);
void teko_rt_run(void);
// Phase 14 (14.I) — multi-argument routine spawn (Go-style). teko_rt_spawn_setarg stages the
// idx-th argument; teko_rt_spawn_args(slot) enqueues the routine with the staged argument vector.
// The routine receives a pointer to its vector and reads args[i] (OP_LOAD_SPAWN_ARG).
void teko_rt_spawn_setarg(long idx, long val);
void teko_rt_spawn_args(long slot);

// Phase 14 (real-time clock) — a portable MONOTONIC nanosecond clock; the time base for the
// cooperative waiters/delays/timeouts. Native: CLOCK_MONOTONIC / QueryPerformanceCounter; WASM:
// imports env.teko_now_ns. Only differences are meaningful (arbitrary epoch).
long long teko_rt_now_ns(void);

// Phase 14 (14.G) — timespan waiters (real time, cooperative, non-blocking). teko_rt_wait_ns
// (`wait <ts>;` → OP_WAIT, in teko_rt.c) spins on the real clock until `ms` ms have elapsed.
// teko_rt_await_ns (`await <ts>;` → OP_AWAIT_FOR, in the scheduler TU) ALSO drains the run queue
// while waiting so queued background tasks run. Both honor the real monotonic clock; neither
// blocks the OS thread in the kernel.
void teko_rt_wait_ns(long ms);
void teko_rt_await_ns(long ms);

// Phase 14 (14.B) — duplex channel surface wrappers (OP_DUPLEX_* lower to these). The handle
// is a TekoDuplex* carried as a register-width integer; values/statuses are i32 at the surface.
long teko_rt_duplex_open(long capacity);
long teko_rt_duplex_send(long handle, long endpoint, long value);
long teko_rt_duplex_recv(long handle, long endpoint);
long teko_rt_duplex_poll(long handle, long endpoint);
long teko_rt_duplex_close(long handle);

// Phase 14 (14.C) — delayed (timed) channel surface wrappers (OP_DELAYED_* lower to these). The
// time base is the real MONOTONIC clock (the wrappers call teko_rt_now_ns); a message is due once
// REAL time has advanced by its `delay` ms — there is no logical `advance` (owner decision).
long teko_rt_delayed_open(long capacity);
long teko_rt_delayed_send(long handle, long value, long delay_ms);
long teko_rt_delayed_recv(long handle);
long teko_rt_delayed_poll(long handle);
long teko_rt_delayed_close(long handle);

// Phase 14 (14.D) — broadcast (1:N pub-sub) channel surface wrappers (OP_BCAST_* lower to these).
long teko_rt_bcast_open(long capacity);
long teko_rt_bcast_subscribe(long handle);
long teko_rt_bcast_publish(long handle, long value);
long teko_rt_bcast_recv(long handle, long sub_id);
long teko_rt_bcast_poll(long handle, long sub_id);
long teko_rt_bcast_close(long handle);

// Phase 14 (14.F) — resilience policy surface wrappers (OP_RETRY_*/OP_CIRCUIT_* lower to these).
// Handles are TekoRetry*/TekoCircuit* as register-width integers; counts/times are i32 (ms).
long teko_rt_retry_new(long attempts, long timeout, long mode, long base);
long teko_rt_retry_should_continue(long handle, long attempt, long elapsed);
long teko_rt_retry_next_delay(long handle, long attempt);
long teko_rt_circuit_new(long threshold, long cooldown);
long teko_rt_circuit_allow(long handle, long now);
long teko_rt_circuit_record(long handle, long ok, long now);

#endif // TEKO_RT_H
