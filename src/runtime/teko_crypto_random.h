#ifndef TEKO_CRYPTO_RANDOM_H
#define TEKO_CRYPTO_RANDOM_H

// Native cryptographically-secure RNG (Phase 13.3a). Thin portable wrapper over the
// platform CSPRNG — `getrandom` (Linux), `arc4random_buf` (macOS/BSD), `BCryptGenRandom`
// (Windows), with a `/dev/urandom` fallback. No external libraries. There is no KAT for a
// CSPRNG (output is non-deterministic); correctness is exercised by sanity/fill/distinct
// tests in tests/runtime/test_crypto_random.c.
//
// WASM note: the cooperative `wasm` target has no OS entropy source — it requires a host
// import. That import is sequenced with the rest of the deferred WASM crypto lowering
// (see docs/PHASE13_NATIVE_CRYPTO.md); this C path serves the 16 native targets.

#include <stdint.h>
#include <stddef.h>

// Fill out[0..len) with cryptographically-secure random bytes.
// Returns 0 on success, non-zero on failure (entropy source unavailable).
int teko_csprng_bytes(uint8_t* out, size_t len);

#endif // TEKO_CRYPTO_RANDOM_H
