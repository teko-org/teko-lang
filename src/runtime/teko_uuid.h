#ifndef TEKO_UUID_H
#define TEKO_UUID_H

// Native UUID / GUID primitive (RFC 4122 / RFC 9562). Pure, portable C23 — no external
// libraries. Built on Teko's MD5 (v3), SHA-1 (v5), and CSPRNG (v4/v7). KAT-tested against
// the RFC namespace vectors in tests/runtime/test_uuid.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_UUID_LEN     16u   // raw bytes
#define TEKO_UUID_STR_LEN 36u   // canonical "8-4-4-4-12" (no NUL)

// Predefined RFC 4122 namespace UUIDs (raw bytes), for v3/v5.
extern const uint8_t TEKO_UUID_NS_DNS[16];
extern const uint8_t TEKO_UUID_NS_URL[16];
extern const uint8_t TEKO_UUID_NS_OID[16];
extern const uint8_t TEKO_UUID_NS_X500[16];

void teko_uuid_nil(uint8_t out[TEKO_UUID_LEN]);

// v3 = MD5(namespace || name); v5 = SHA-1(namespace || name) truncated. Deterministic.
void teko_uuid_v3(uint8_t out[TEKO_UUID_LEN], const uint8_t ns[16], const uint8_t* name, size_t name_len);
void teko_uuid_v5(uint8_t out[TEKO_UUID_LEN], const uint8_t ns[16], const uint8_t* name, size_t name_len);

// v4 = random (CSPRNG). Returns 0 on success, -1 if entropy is unavailable.
int teko_uuid_v4(uint8_t out[TEKO_UUID_LEN]);

// v7 = 48-bit big-endian Unix-ms timestamp + random tail (time-ordered). `unix_ms` is passed
// in so the caller controls the clock (and tests are deterministic in structure). Returns 0/-1.
int teko_uuid_v7(uint8_t out[TEKO_UUID_LEN], uint64_t unix_ms);

// v8 = custom (RFC 9562): 122 caller-defined bits with the version-8 + 10xx-variant bits
// stamped over the supplied 16 bytes. Fully deterministic.
void teko_uuid_v8(uint8_t out[TEKO_UUID_LEN], const uint8_t data[TEKO_UUID_LEN]);

// Canonical lowercase format into a 36-char buffer (no NUL written).
void teko_uuid_format(char out[TEKO_UUID_STR_LEN], const uint8_t uuid[TEKO_UUID_LEN]);

// Parse a canonical string (36 chars, 8-4-4-4-12, hex, dashes at 8/13/18/23; case-insensitive).
// Returns 0 on success, -1 on malformed input.
int teko_uuid_parse(uint8_t out[TEKO_UUID_LEN], const char* str);

// Extract the version (1..8) and the RFC-4122 variant flag (1 if the 10xx variant).
int teko_uuid_version(const uint8_t uuid[TEKO_UUID_LEN]);
int teko_uuid_is_rfc4122_variant(const uint8_t uuid[TEKO_UUID_LEN]);

#endif // TEKO_UUID_H
