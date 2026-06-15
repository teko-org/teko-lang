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
#include "teko_crypto_x25519.h"
#include "teko_crypto_hkdf.h"
#include "teko_crypto_pbkdf2.h"
#include "teko_crypto_p256.h"
#include "teko_crypto_p384.h"
#include "teko_crypto_rsa.h"
#include "teko_crypto_random.h"
#include "teko_uuid.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#if !defined(__wasm__)
// stdio/time back the print primitive (teko_rt_emit) and the UUID v4/v7 clock —
// neither exists in the freestanding wasm32 build. On WASM the crypto reactor
// exports only the pure crypto wrappers (ids 4-40); emit + uuid.v4/v7 + random
// (ids 41-43) are lowered in-module by the emitter, so guard them out here.
#include <stdio.h>
#include <time.h>
#if defined(_WIN32)
#include <windows.h> // GetSystemTimeAsFileTime (no POSIX clock_gettime on MSVC)
#endif
#endif

#define TEKO_RT_KDF_MAX_OUT 1024 // bound variable-length output buffers (KDF/XOF) to a sane size

// See teko_rt.h. This translation unit is compiled into the static archive
// `libteko_rt.a`, which the native runner links into every produced executable.
// Crypto wrappers (Sub-phase B) are added alongside this print primitive; they call
// the portable C crypto runtime (the single source of truth), which is compiled into
// the same archive so produced binaries are self-contained.

#if !defined(__wasm__)
void teko_rt_emit(const char* s) {
    puts(s ? s : "");
}
#endif

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

// SHAKE128/256 XOF: msg is a plain string, out_len is the requested squeeze length (bytes).
// Returns the lowercase-hex of `out_len` output bytes. Output bounded like the KDFs.
char* teko_rt_shake128(const char* msg, int out_len) {
    if (out_len <= 0 || out_len > TEKO_RT_KDF_MAX_OUT) return NULL;
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t* out = (uint8_t*)malloc((size_t)out_len);
    if (!out) return NULL;
    teko_shake128(p, len, out, (size_t)out_len);
    char* hex = teko_rt_to_hex(out, (size_t)out_len);
    free(out);
    return hex;
}

char* teko_rt_shake256(const char* msg, int out_len) {
    if (out_len <= 0 || out_len > TEKO_RT_KDF_MAX_OUT) return NULL;
    const uint8_t* p = (const uint8_t*)(msg ? msg : "");
    size_t len = msg ? strlen(msg) : 0;
    uint8_t* out = (uint8_t*)malloc((size_t)out_len);
    if (!out) return NULL;
    teko_shake256(p, len, out, (size_t)out_len);
    char* hex = teko_rt_to_hex(out, (size_t)out_len);
    free(out);
    return hex;
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

// --- Key exchange: X25519 --------------------------------------------------------
char* teko_rt_x25519(const char* scalar_hex, const char* u_hex) {
    size_t sl = 0, ul = 0;
    uint8_t* scalar = teko_rt_from_hex(scalar_hex, &sl);
    uint8_t* u = teko_rt_from_hex(u_hex, &ul);
    char* out = NULL;
    if (scalar && u && sl == TEKO_X25519_LEN && ul == TEKO_X25519_LEN) {
        uint8_t shared[TEKO_X25519_LEN];
        teko_x25519(shared, scalar, u);
        out = teko_rt_to_hex(shared, TEKO_X25519_LEN);
    }
    free(scalar); free(u);
    return out;
}

// --- KDF: HKDF-SHA-256 / PBKDF2-HMAC-SHA-256 -------------------------------------
char* teko_rt_hkdf_sha256(const char* ikm_hex, const char* salt_hex,
                          const char* info_hex, int out_len) {
    if (out_len <= 0 || out_len > TEKO_RT_KDF_MAX_OUT) return NULL;
    size_t il = 0, sl = 0, fl = 0;
    uint8_t* ikm = teko_rt_from_hex(ikm_hex, &il);
    uint8_t* salt = teko_rt_from_hex(salt_hex, &sl);
    uint8_t* info = teko_rt_from_hex(info_hex, &fl);
    char* out = NULL;
    if (ikm && salt && info) {
        uint8_t* okm = (uint8_t*)malloc((size_t)out_len);
        if (okm) {
            if (teko_hkdf_sha256(salt, sl, ikm, il, info, fl, okm, (size_t)out_len) == 0)
                out = teko_rt_to_hex(okm, (size_t)out_len);
            free(okm);
        }
    }
    free(ikm); free(salt); free(info);
    return out;
}

char* teko_rt_pbkdf2_sha256(const char* pass_hex, const char* salt_hex,
                            int iterations, int dk_len) {
    if (iterations <= 0 || dk_len <= 0 || dk_len > TEKO_RT_KDF_MAX_OUT) return NULL;
    size_t pl = 0, sl = 0;
    uint8_t* pass = teko_rt_from_hex(pass_hex, &pl);
    uint8_t* salt = teko_rt_from_hex(salt_hex, &sl);
    char* out = NULL;
    if (pass && salt) {
        uint8_t* dk = (uint8_t*)malloc((size_t)dk_len);
        if (dk) {
            if (teko_pbkdf2_hmac_sha256(pass, pl, salt, sl, (uint32_t)iterations,
                                        dk, (size_t)dk_len) == 0)
                out = teko_rt_to_hex(dk, (size_t)dk_len);
            free(dk);
        }
    }
    free(pass); free(salt);
    return out;
}

// --- ECDSA over NIST P-256 / P-384 -----------------------------------------------
// Boolean surface result: a fresh "1"/"0" string.
static char* teko_rt_bool(int ok) {
    char* s = (char*)malloc(2);
    if (s) { s[0] = ok ? '1' : '0'; s[1] = '\0'; }
    return s;
}

char* teko_rt_ecdsa_p256_sign(const char* priv_hex, const char* hash_hex) {
    size_t pl = 0, hl = 0;
    uint8_t* priv = teko_rt_from_hex(priv_hex, &pl);
    uint8_t* hash = teko_rt_from_hex(hash_hex, &hl);
    char* out = NULL;
    if (priv && hash && pl == 32) {
        uint8_t sig[64];
        if (teko_p256_ecdsa_sign(priv, hash, hl, sig) == 0) out = teko_rt_to_hex(sig, 64);
    }
    free(priv); free(hash);
    return out;
}

char* teko_rt_ecdsa_p256_verify(const char* pub_hex, const char* hash_hex, const char* sig_hex) {
    size_t pl = 0, hl = 0, sl = 0;
    uint8_t* pub = teko_rt_from_hex(pub_hex, &pl);
    uint8_t* hash = teko_rt_from_hex(hash_hex, &hl);
    uint8_t* sig = teko_rt_from_hex(sig_hex, &sl);
    int ok = (pub && hash && sig && pl == 64 && sl == 64 &&
              teko_p256_ecdsa_verify(pub, hash, hl, sig) == 0);
    free(pub); free(hash); free(sig);
    return teko_rt_bool(ok);
}

char* teko_rt_ecdsa_p384_sign(const char* priv_hex, const char* hash_hex) {
    size_t pl = 0, hl = 0;
    uint8_t* priv = teko_rt_from_hex(priv_hex, &pl);
    uint8_t* hash = teko_rt_from_hex(hash_hex, &hl);
    char* out = NULL;
    if (priv && hash && pl == 48) {
        uint8_t sig[96];
        if (teko_p384_ecdsa_sign(priv, hash, hl, sig) == 0) out = teko_rt_to_hex(sig, 96);
    }
    free(priv); free(hash);
    return out;
}

char* teko_rt_ecdsa_p384_verify(const char* pub_hex, const char* hash_hex, const char* sig_hex) {
    size_t pl = 0, hl = 0, sl = 0;
    uint8_t* pub = teko_rt_from_hex(pub_hex, &pl);
    uint8_t* hash = teko_rt_from_hex(hash_hex, &hl);
    uint8_t* sig = teko_rt_from_hex(sig_hex, &sl);
    int ok = (pub && hash && sig && pl == 96 && sl == 96 &&
              teko_p384_ecdsa_verify(pub, hash, hl, sig) == 0);
    free(pub); free(hash); free(sig);
    return teko_rt_bool(ok);
}

// --- RSA: PSS sign/verify, OAEP encrypt/decrypt (SHA-256 + MGF1-SHA-256) ---------
#define TEKO_RT_RSA_PSS_SALT_LEN 32u // = hLen for SHA-256 (randomized salt, secure default)

char* teko_rt_rsa_pss_sign(const char* n_hex, const char* d_hex, const char* mhash_hex) {
    size_t nl = 0, dl = 0, ml = 0;
    uint8_t* n = teko_rt_from_hex(n_hex, &nl);
    uint8_t* d = teko_rt_from_hex(d_hex, &dl);
    uint8_t* mh = teko_rt_from_hex(mhash_hex, &ml);
    char* out = NULL;
    if (n && d && mh && ml == 32) {
        uint8_t* sig = (uint8_t*)malloc(nl);
        if (sig) {
            if (teko_rsa_pss_sign(n, nl, d, dl, TEKO_RSA_SHA256, mh, ml,
                                  TEKO_RT_RSA_PSS_SALT_LEN, sig) == 0)
                out = teko_rt_to_hex(sig, nl);
            free(sig);
        }
    }
    free(n); free(d); free(mh);
    return out;
}

char* teko_rt_rsa_pss_verify(const char* n_hex, const char* e_hex,
                             const char* mhash_hex, const char* sig_hex) {
    size_t nl = 0, el = 0, ml = 0, sl = 0;
    uint8_t* n = teko_rt_from_hex(n_hex, &nl);
    uint8_t* e = teko_rt_from_hex(e_hex, &el);
    uint8_t* mh = teko_rt_from_hex(mhash_hex, &ml);
    uint8_t* sig = teko_rt_from_hex(sig_hex, &sl);
    int ok = (n && e && mh && sig && ml == 32 &&
              teko_rsa_pss_verify(n, nl, e, el, TEKO_RSA_SHA256, mh, ml,
                                  TEKO_RT_RSA_PSS_SALT_LEN, sig, sl) == 0);
    free(n); free(e); free(mh); free(sig);
    return teko_rt_bool(ok);
}

char* teko_rt_rsa_oaep_encrypt(const char* n_hex, const char* e_hex, const char* msg_hex) {
    size_t nl = 0, el = 0, ml = 0;
    uint8_t* n = teko_rt_from_hex(n_hex, &nl);
    uint8_t* e = teko_rt_from_hex(e_hex, &el);
    uint8_t* msg = teko_rt_from_hex(msg_hex, &ml);
    char* out = NULL;
    if (n && e && msg) {
        uint8_t* ct = (uint8_t*)malloc(nl);
        if (ct) {
            if (teko_rsa_oaep_encrypt(n, nl, e, el, TEKO_RSA_SHA256, NULL, 0, msg, ml, ct) == 0)
                out = teko_rt_to_hex(ct, nl);
            free(ct);
        }
    }
    free(n); free(e); free(msg);
    return out;
}

char* teko_rt_rsa_oaep_decrypt(const char* n_hex, const char* d_hex, const char* ct_hex) {
    size_t nl = 0, dl = 0, cl = 0;
    uint8_t* n = teko_rt_from_hex(n_hex, &nl);
    uint8_t* d = teko_rt_from_hex(d_hex, &dl);
    uint8_t* ct = teko_rt_from_hex(ct_hex, &cl);
    char* out = NULL;
    if (!n || !d || !ct) {
        out = teko_rt_reject();
    } else {
        uint8_t* rec = (uint8_t*)malloc(nl ? nl : 1);
        if (rec) {
            size_t rlen = 0;
            int rc = teko_rsa_oaep_decrypt(n, nl, d, dl, TEKO_RSA_SHA256, NULL, 0,
                                           ct, cl, rec, &rlen);
            out = (rc == 0) ? teko_rt_to_hex(rec, rlen) : teko_rt_reject();
            free(rec);
        }
    }
    free(n); free(d); free(ct);
    return out;
}

// --- CSPRNG ----------------------------------------------------------------------
// ids 41-43 (random.bytes, uuid.v4/v7) are lowered in-module on WASM (host
// entropy/time imports), so the reactor doesn't export them — guard out the
// clock/CSPRNG-string wrappers that would otherwise pull stdio/time on wasm32.
#if !defined(__wasm__)
char* teko_rt_random_bytes(int n) {
    if (n <= 0 || n > TEKO_RT_KDF_MAX_OUT) return NULL;
    uint8_t* buf = (uint8_t*)malloc((size_t)n);
    if (!buf) return NULL;
    char* out = NULL;
    if (teko_csprng_bytes(buf, (size_t)n) == 0) out = teko_rt_to_hex(buf, (size_t)n);
    free(buf);
    return out;
}

// --- UUID v4 / v7 ----------------------------------------------------------------
// Canonical-format a 16-byte UUID into a fresh NUL-terminated 36-char string.
static char* teko_rt_uuid_str(const uint8_t u[16]) {
    char* out = (char*)malloc(TEKO_UUID_STR_LEN + 1);
    if (!out) return NULL;
    teko_uuid_format(out, u);          // writes 36 chars, no NUL
    out[TEKO_UUID_STR_LEN] = '\0';
    return out;
}

char* teko_rt_uuid_v4(int ignored) {
    (void)ignored;
    uint8_t u[TEKO_UUID_LEN];
    if (teko_uuid_v4(u) != 0) return NULL;
    return teko_rt_uuid_str(u);
}

// Current Unix time in milliseconds, portably across the host toolchains.
static uint64_t teko_rt_unix_ms(void) {
#if defined(_WIN32)
    // FILETIME is 100ns ticks since 1601-01-01; 116444736000000000 ticks to the Unix epoch.
    FILETIME ft;
    ULARGE_INTEGER u;
    GetSystemTimeAsFileTime(&ft);
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return (uint64_t)((u.QuadPart - 116444736000000000ULL) / 10000ULL);
#elif defined(CLOCK_REALTIME)
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0)
        return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000l);
    return (uint64_t)time(NULL) * 1000ull;
#else
    return (uint64_t)time(NULL) * 1000ull;
#endif
}

char* teko_rt_uuid_v7(int ignored) {
    (void)ignored;
    uint8_t u[TEKO_UUID_LEN];
    if (teko_uuid_v7(u, teko_rt_unix_ms()) != 0) return NULL;
    return teko_rt_uuid_str(u);
}
#endif // !__wasm__ (CSPRNG / UUID v4-v7 tail)
