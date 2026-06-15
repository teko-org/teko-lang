#include "teko_rt.h"
#include "teko_crypto_sha2.h"
#include "teko_crypto_sha512.h"
#include "teko_crypto_sha3.h"
#include "teko_crypto_blake3.h"
#include "teko_crypto_blake2b.h"
#include "teko_crypto_hmac.h"
#include "teko_crypto_aes.h"
#include "teko_crypto_aes_gcm.h"
#include "teko_crypto_chachapoly.h"
#include "teko_crypto_ed25519.h"
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

// Decode a hex string into a fresh byte buffer (caller frees via free). Sets *out_len.
// Returns NULL on odd length or a non-hex digit. An empty string decodes to a 0-length
// buffer (a 1-byte allocation, so the result is never NULL for valid input).
static int hexval(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
static uint8_t* teko_rt_from_hex(const char* hex, size_t* out_len) {
    if (!hex) return NULL;
    size_t hl = strlen(hex);
    if (hl % 2 != 0) return NULL;
    size_t n = hl / 2;
    uint8_t* out = (uint8_t*)malloc(n ? n : 1);
    if (!out) return NULL;
    for (size_t i = 0; i < n; i++) {
        int hi = hexval((unsigned char)hex[2 * i]);
        int lo = hexval((unsigned char)hex[2 * i + 1]);
        if (hi < 0 || lo < 0) { free(out); return NULL; }
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    if (out_len) *out_len = n;
    return out;
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

char* teko_rt_sha384_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA384_DIGEST_LEN];
    teko_sha384(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA384_DIGEST_LEN);
}

char* teko_rt_sha512_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA512_DIGEST_LEN];
    teko_sha512(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA512_DIGEST_LEN);
}

char* teko_rt_sha3_256_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA3_256_DIGEST_LEN];
    teko_sha3_256(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA3_256_DIGEST_LEN);
}

char* teko_rt_sha3_512_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_SHA3_512_DIGEST_LEN];
    teko_sha3_512(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_SHA3_512_DIGEST_LEN);
}

char* teko_rt_blake3_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[TEKO_BLAKE3_OUT_LEN];
    teko_blake3(p, len, digest);
    return teko_rt_to_hex(digest, TEKO_BLAKE3_OUT_LEN);
}

char* teko_rt_blake2b_hex(const char* msg) {
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t digest[64];
    if (teko_blake2b(p, len, digest, 64u) != 0) return NULL;
    return teko_rt_to_hex(digest, 64u);
}

char* teko_rt_hmac_sha256(const char* key_hex, const char* msg) {
    size_t klen = 0;
    uint8_t* key = teko_rt_from_hex(key_hex, &klen);
    if (!key) return NULL;
    uint8_t mac[TEKO_SHA256_DIGEST_LEN];
    teko_hmac_sha256(key, klen, (const uint8_t*)(msg ? msg : ""), msg ? strlen(msg) : 0, mac);
    free(key);
    return teko_rt_to_hex(mac, TEKO_SHA256_DIGEST_LEN);
}

char* teko_rt_hmac_sha384(const char* key_hex, const char* msg) {
    size_t klen = 0;
    uint8_t* key = teko_rt_from_hex(key_hex, &klen);
    if (!key) return NULL;
    uint8_t mac[TEKO_SHA384_DIGEST_LEN];
    teko_hmac_sha384(key, klen, (const uint8_t*)(msg ? msg : ""), msg ? strlen(msg) : 0, mac);
    free(key);
    return teko_rt_to_hex(mac, TEKO_SHA384_DIGEST_LEN);
}

char* teko_rt_hmac_sha512(const char* key_hex, const char* msg) {
    size_t klen = 0;
    uint8_t* key = teko_rt_from_hex(key_hex, &klen);
    if (!key) return NULL;
    uint8_t mac[TEKO_SHA512_DIGEST_LEN];
    teko_hmac_sha512(key, klen, (const uint8_t*)(msg ? msg : ""), msg ? strlen(msg) : 0, mac);
    free(key);
    return teko_rt_to_hex(mac, TEKO_SHA512_DIGEST_LEN);
}

// --- AEAD ------------------------------------------------------------------------
// The "open" sentinel for a tag-verification failure or malformed input.
#define TEKO_RT_AEAD_TAG_LEN 16u
static char* teko_rt_reject(void) {
    char* s = (char*)malloc(7);
    if (s) memcpy(s, "REJECT", 7);
    return s;
}

char* teko_rt_aes_gcm_seal(const char* key_hex, const char* nonce_hex,
                           const char* aad_hex, const char* pt_hex) {
    size_t kl = 0, nl = 0, al = 0, pl = 0;
    uint8_t* k = teko_rt_from_hex(key_hex, &kl);
    uint8_t* n = teko_rt_from_hex(nonce_hex, &nl);
    uint8_t* a = teko_rt_from_hex(aad_hex, &al);
    uint8_t* p = teko_rt_from_hex(pt_hex, &pl);
    char* out = NULL;
    TekoAesKey key;
    if (k && n && a && p && teko_aes_init(&key, k, kl) == 0) {
        uint8_t* ctt = (uint8_t*)malloc(pl + TEKO_RT_AEAD_TAG_LEN);
        if (ctt) {
            teko_aes_gcm_encrypt(&key, n, nl, a, al, p, pl, ctt, ctt + pl, TEKO_RT_AEAD_TAG_LEN);
            out = teko_rt_to_hex(ctt, pl + TEKO_RT_AEAD_TAG_LEN);
            free(ctt);
        }
    }
    free(k); free(n); free(a); free(p);
    return out;
}

char* teko_rt_aes_gcm_open(const char* key_hex, const char* nonce_hex,
                           const char* aad_hex, const char* ct_tag_hex) {
    size_t kl = 0, nl = 0, al = 0, cl = 0;
    uint8_t* k = teko_rt_from_hex(key_hex, &kl);
    uint8_t* n = teko_rt_from_hex(nonce_hex, &nl);
    uint8_t* a = teko_rt_from_hex(aad_hex, &al);
    uint8_t* ct = teko_rt_from_hex(ct_tag_hex, &cl);
    char* out = NULL;
    TekoAesKey key;
    if (!k || !n || !a || !ct || cl < TEKO_RT_AEAD_TAG_LEN || teko_aes_init(&key, k, kl) != 0) {
        out = teko_rt_reject();
    } else {
        size_t ctlen = cl - TEKO_RT_AEAD_TAG_LEN;
        uint8_t* pt = (uint8_t*)malloc(ctlen ? ctlen : 1);
        if (pt) {
            int rc = teko_aes_gcm_decrypt(&key, n, nl, a, al, ct, ctlen,
                                          ct + ctlen, TEKO_RT_AEAD_TAG_LEN, pt);
            out = (rc == 0) ? teko_rt_to_hex(pt, ctlen) : teko_rt_reject();
            free(pt);
        }
    }
    free(k); free(n); free(a); free(ct);
    return out;
}

char* teko_rt_chacha20poly1305_seal(const char* key_hex, const char* nonce_hex,
                                    const char* aad_hex, const char* pt_hex) {
    size_t kl = 0, nl = 0, al = 0, pl = 0;
    uint8_t* k = teko_rt_from_hex(key_hex, &kl);
    uint8_t* n = teko_rt_from_hex(nonce_hex, &nl);
    uint8_t* a = teko_rt_from_hex(aad_hex, &al);
    uint8_t* p = teko_rt_from_hex(pt_hex, &pl);
    char* out = NULL;
    if (k && n && a && p && kl == TEKO_CHACHAPOLY_KEY_LEN && nl == TEKO_CHACHAPOLY_NONCE_LEN) {
        uint8_t* ctt = (uint8_t*)malloc(pl + TEKO_CHACHAPOLY_TAG_LEN);
        if (ctt) {
            teko_chacha20poly1305_encrypt(k, n, a, al, p, pl, ctt, ctt + pl);
            out = teko_rt_to_hex(ctt, pl + TEKO_CHACHAPOLY_TAG_LEN);
            free(ctt);
        }
    }
    free(k); free(n); free(a); free(p);
    return out;
}

char* teko_rt_chacha20poly1305_open(const char* key_hex, const char* nonce_hex,
                                    const char* aad_hex, const char* ct_tag_hex) {
    size_t kl = 0, nl = 0, al = 0, cl = 0;
    uint8_t* k = teko_rt_from_hex(key_hex, &kl);
    uint8_t* n = teko_rt_from_hex(nonce_hex, &nl);
    uint8_t* a = teko_rt_from_hex(aad_hex, &al);
    uint8_t* ct = teko_rt_from_hex(ct_tag_hex, &cl);
    char* out = NULL;
    if (!k || !n || !a || !ct || kl != TEKO_CHACHAPOLY_KEY_LEN ||
        nl != TEKO_CHACHAPOLY_NONCE_LEN || cl < TEKO_CHACHAPOLY_TAG_LEN) {
        out = teko_rt_reject();
    } else {
        size_t ctlen = cl - TEKO_CHACHAPOLY_TAG_LEN;
        uint8_t* pt = (uint8_t*)malloc(ctlen ? ctlen : 1);
        if (pt) {
            int rc = teko_chacha20poly1305_decrypt(k, n, a, al, ct, ctlen, ct + ctlen, pt);
            out = (rc == 0) ? teko_rt_to_hex(pt, ctlen) : teko_rt_reject();
            free(pt);
        }
    }
    free(k); free(n); free(a); free(ct);
    return out;
}

// --- Signatures: Ed25519 ---------------------------------------------------------
char* teko_rt_ed25519_sign(const char* seed_hex, const char* msg_hex) {
    size_t sl = 0, ml = 0;
    uint8_t* seed = teko_rt_from_hex(seed_hex, &sl);
    uint8_t* msg = teko_rt_from_hex(msg_hex, &ml);
    char* out = NULL;
    if (seed && msg && sl == TEKO_ED25519_SEED_LEN) {
        uint8_t pub[TEKO_ED25519_PUB_LEN];
        uint8_t sig[TEKO_ED25519_SIG_LEN];
        teko_ed25519_pubkey(pub, seed);
        teko_ed25519_sign(sig, msg, ml, seed, pub);
        out = teko_rt_to_hex(sig, TEKO_ED25519_SIG_LEN);
    }
    free(seed); free(msg);
    return out;
}

char* teko_rt_ed25519_verify(const char* pub_hex, const char* msg_hex, const char* sig_hex) {
    size_t pl = 0, ml = 0, gl = 0;
    uint8_t* pub = teko_rt_from_hex(pub_hex, &pl);
    uint8_t* msg = teko_rt_from_hex(msg_hex, &ml);
    uint8_t* sig = teko_rt_from_hex(sig_hex, &gl);
    int ok = 0;
    if (pub && msg && sig && pl == TEKO_ED25519_PUB_LEN && gl == TEKO_ED25519_SIG_LEN) {
        ok = (teko_ed25519_verify(sig, msg, ml, pub) == 0);
    }
    free(pub); free(msg); free(sig);
    char* out = (char*)malloc(2);
    if (out) { out[0] = ok ? '1' : '0'; out[1] = '\0'; }
    return out;
}
