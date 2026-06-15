#include "teko_rt.h"
#include "teko_crypto_sha2.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// See teko_rt.h. This translation unit is compiled into the static archive
// `libteko_rt.a`, which the native runner links into every produced executable.
// Crypto wrappers (Sub-phase B) are added alongside this print primitive; they call
// the portable C crypto runtime (the single source of truth), which is compiled into
// the same archive so produced binaries are self-contained.

void teko_rt_emit(const char* s) {
    puts(s ? s : "");
}

// Lowercase-hex-encode n bytes into a fresh NUL-terminated string (caller-owned).
static char* teko_rt_to_hex(const uint8_t* b, size_t n) {
    static const char hexd[] = "0123456789abcdef";
    char* out = (char*)malloc(n * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        out[2 * i]     = hexd[(b[i] >> 4) & 0xF];
        out[2 * i + 1] = hexd[b[i] & 0xF];
    }
    out[2 * n] = '\0';
    return out;
}

char* teko_rt_sha256_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA256_DIGEST_LEN];
    teko_sha256(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA256_DIGEST_LEN);
}
