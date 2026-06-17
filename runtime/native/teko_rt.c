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
#include "teko_duplex.h"
#include "teko_delayed.h"
#include "teko_broadcast.h"
#include "teko_retry.h"
#include "teko_time.h"
#include "teko_object.h"
#include "teko_vtable.h"
#include "teko_array.h"
#include "teko_iarray.h"
#include "teko_convert.h"
#include "teko_decimal.h"
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

// Phase 14 (real-time clock): a portable MONOTONIC nanosecond clock — the time base for the
// cooperative waiters/delays/timeouts (it replaced the old logical clock). Native uses
// CLOCK_MONOTONIC (POSIX/macOS, monotonic — NOT realtime) / QueryPerformanceCounter (Windows/MSVC);
// WASM imports env.teko_now_ns (Node: process.hrtime.bigint() — real ns; browser:
// performance.now()*1e6, best-effort — the browser clamps/coarsens performance.now() for security,
// so sub-ms resolution is not guaranteed there; see the run-*.mjs harnesses). Returns a 64-bit ns
// count from an arbitrary epoch — only DIFFERENCES are meaningful (deadlines/elapsed).
#if defined(__wasm__)
__attribute__((import_module("env"), import_name("teko_now_ns")))
extern long long teko_rt_host_now_ns(void);
long long teko_rt_now_ns(void) { return teko_rt_host_now_ns(); }
#elif defined(_WIN32)
long long teko_rt_now_ns(void) {
    LARGE_INTEGER freq, cnt;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&cnt);
    long long f = freq.QuadPart ? freq.QuadPart : 1;
    long long sec = cnt.QuadPart / f, rem = cnt.QuadPart % f;
    return sec * 1000000000LL + (rem * 1000000000LL) / f; // ns, overflow-safe
}
#else
long long teko_rt_now_ns(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts); // fallback if monotonic is unavailable
#endif
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}
#endif

// See teko_rt.h. This translation unit is compiled into the static archive
// `libteko_rt.a`, which the native runner links into every produced executable.
// Crypto wrappers (Sub-phase B) are added alongside this print primitive; they call
// the portable C crypto runtime (the single source of truth), which is compiled into
// the same archive so produced binaries are self-contained.

#if !defined(__wasm__)
void teko_rt_emit(const char* s) {
    puts(s ? s : "");
}
// Phase 14: print an integer (one per line) — lets concurrency proofs surface i32 results
// (channel values, statuses) the same way teko_rt_emit surfaces strings.
void teko_rt_emit_int(long n) {
    printf("%ld\n", n);
}

// Phase 14 (14.G): `wait <ts>;` — a SYNCHRONOUS real-time wait for `ms` canonical milliseconds (the
// frontend folded any unit suffix to ms). It returns only once the real MONOTONIC clock has
// advanced by at least the requested span — checked cooperatively against the ns clock, WITHOUT
// blocking the OS thread in the kernel (owner decision: non-blocking cooperative scheduling; the
// time source is real). A non-positive delay is a no-op. `await` (teko_rt_await_ns) is the variant
// that also drains the run queue while waiting. Lowered from OP_WAIT (ms in arg0).
void teko_rt_wait_ns(long ms) {
    if (ms <= 0) return;
    long long deadline = teko_rt_now_ns() + (long long)ms * 1000000LL;
    while (teko_rt_now_ns() < deadline) { /* cooperative spin on the real clock — no kernel block */ }
}
#endif

// Phase 14 (14.B) — duplex channel surface wrappers. The OP_DUPLEX_* opcodes lower to these
// (SysV/AAPCS calls); the duplex C runtime (src/runtime/teko_duplex.c) is the source of truth.
// The handle is the TekoDuplex* carried through the surface as a register-width integer; all
// values/statuses are i32 at the .tks level. Available on every target (native + wasm reactor).
long teko_rt_duplex_open(long capacity) {
    return (long)(intptr_t)teko_duplex_open((uint32_t)capacity);
}
long teko_rt_duplex_send(long handle, long endpoint, long value) {
    return (long)teko_duplex_send((TekoDuplex*)(intptr_t)handle, (int)endpoint, (int32_t)value);
}
long teko_rt_duplex_recv(long handle, long endpoint) {
    int32_t v = 0;
    (void)teko_duplex_recv((TekoDuplex*)(intptr_t)handle, (int)endpoint, &v);
    return (long)v; // value (0 when empty/closed — callers probe status via duplex.poll)
}
long teko_rt_duplex_poll(long handle, long endpoint) {
    return (long)teko_duplex_poll((TekoDuplex*)(intptr_t)handle, (int)endpoint);
}
long teko_rt_duplex_close(long handle) {
    teko_duplex_close((TekoDuplex*)(intptr_t)handle);
    return 0;
}

// Phase 14 (14.C) — delayed (timed) channel surface wrappers (OP_DELAYED_* lower to these).
long teko_rt_delayed_open(long capacity) {
    return (long)(intptr_t)teko_delayed_open((uint32_t)capacity);
}
// Phase 14 (real-time clock): the delay is canonical ms (i32 surface); the deadline math is real
// ns. The wrapper reads the real MONOTONIC clock (teko_rt_now_ns) and passes it to the runtime, so
// a message becomes due once REAL time has advanced by `delay` ms — no logical `advance` tick.
long teko_rt_delayed_send(long handle, long value, long delay_ms) {
    return (long)teko_delayed_send((TekoDelayed*)(intptr_t)handle, (int32_t)value,
                                   (uint64_t)(long long)delay_ms * 1000000ULL,
                                   (uint64_t)teko_rt_now_ns());
}
long teko_rt_delayed_recv(long handle) {
    int32_t v = 0;
    (void)teko_delayed_recv((TekoDelayed*)(intptr_t)handle, &v, (uint64_t)teko_rt_now_ns());
    return (long)v; // earliest-due value (0 when none due — callers probe via delayed.poll)
}
long teko_rt_delayed_poll(long handle) {
    return (long)teko_delayed_poll((TekoDelayed*)(intptr_t)handle, (uint64_t)teko_rt_now_ns());
}
long teko_rt_delayed_close(long handle) {
    teko_delayed_close((TekoDelayed*)(intptr_t)handle);
    return 0;
}

// Phase 14 (14.D) — broadcast (1:N pub-sub) channel surface wrappers (OP_BCAST_* lower to these).
long teko_rt_bcast_open(long capacity) {
    return (long)(intptr_t)teko_broadcast_open((uint32_t)capacity);
}
long teko_rt_bcast_subscribe(long handle) {
    return (long)teko_broadcast_subscribe((TekoBroadcast*)(intptr_t)handle);
}
long teko_rt_bcast_publish(long handle, long value) {
    return (long)teko_broadcast_publish((TekoBroadcast*)(intptr_t)handle, (int32_t)value);
}
long teko_rt_bcast_recv(long handle, long sub_id) {
    int32_t v = 0;
    (void)teko_broadcast_recv((TekoBroadcast*)(intptr_t)handle, (int)sub_id, &v);
    return (long)v; // value (0 when empty/closed/lagged — callers probe via broadcast.poll)
}
long teko_rt_bcast_poll(long handle, long sub_id) {
    return (long)teko_broadcast_poll((TekoBroadcast*)(intptr_t)handle, (int)sub_id);
}
long teko_rt_bcast_close(long handle) {
    teko_broadcast_close((TekoBroadcast*)(intptr_t)handle);
    return 0;
}

// Phase 14 (14.F) — resilience policy surface wrappers (OP_RETRY_*/OP_CIRCUIT_* lower to these).
// The handle is a TekoRetry*/TekoCircuit* carried as a register-width integer; the time/count
// args are i32 at the surface (ms fit in i32 for the MVP — the teko_retry C policy is the single
// source of truth). mode: 0 = exponential, non-zero = logarithmic.
// Phase 14 (real-time clock): the policy units are canonical ms; the timeout/cooldown are driven
// by the REAL monotonic clock (teko_rt_now_ns, read as ms). The wrappers record the start instant
// and supply real `now`/`elapsed` so the surface ops don't carry a logical time argument.
static long teko_rt_now_ms(void) { return (long)(teko_rt_now_ns() / 1000000LL); }
long teko_rt_retry_new(long attempts, long timeout, long mode, long base) {
    TekoRetry* r = teko_retry_new((int)attempts, (uint64_t)(unsigned long)timeout,
        mode ? TEKO_BACKOFF_LOGARITHMIC : TEKO_BACKOFF_EXPONENTIAL, (uint64_t)(unsigned long)base);
    teko_retry_mark_start(r, (uint64_t)teko_rt_now_ms());
    return (long)(intptr_t)r;
}
long teko_rt_retry_should_continue(long handle, long attempt) {
    return teko_retry_should_continue_rt((const TekoRetry*)(intptr_t)handle, (int)attempt,
                                         (uint64_t)teko_rt_now_ms());
}
long teko_rt_retry_next_delay(long handle, long attempt) {
    return (long)teko_retry_next_delay((const TekoRetry*)(intptr_t)handle, (int)attempt);
}
long teko_rt_circuit_new(long threshold, long cooldown) {
    return (long)(intptr_t)teko_circuit_new((int)threshold, (uint64_t)(unsigned long)cooldown);
}
long teko_rt_circuit_allow(long handle) {
    return teko_circuit_allow((TekoCircuit*)(intptr_t)handle, (uint64_t)teko_rt_now_ms());
}
long teko_rt_circuit_record(long handle, long ok) {
    teko_circuit_record((TekoCircuit*)(intptr_t)handle, (int)ok, (uint64_t)teko_rt_now_ms());
    return 0;
}

// Phase 14 (wall-clock / timezone surface) — the platform facts behind the civil-time surface: the
// current Unix timestamp and the DST-correct local UTC offset for an instant, sourced from the OS
// (native time()/localtime; WASM host imports env.teko_now_unix / env.teko_tz_offset). The portable
// teko_time_* formatter (the source of truth) turns these into the ISO-8601 strings.
#if defined(__wasm__)
__attribute__((import_module("env"), import_name("teko_now_unix")))
extern long long teko_rt_host_now_unix(void);
__attribute__((import_module("env"), import_name("teko_tz_offset")))
extern int teko_rt_host_tz_offset(long long epoch_s);
static long long teko_rt_time_now_s(void) { return teko_rt_host_now_unix(); }
static long teko_rt_time_offset(long long epoch_s) { return (long)teko_rt_host_tz_offset(epoch_s); }
#else
static long long teko_rt_time_now_s(void) { return (long long)time(NULL); }
static long teko_rt_time_offset(long long epoch_s) {
    time_t t = (time_t)epoch_s;
    struct tm lt;
#if defined(_WIN32)
    localtime_s(&lt, &t);
#else
    localtime_r(&t, &lt);
#endif
    // localtime gave DST-adjusted LOCAL fields; expressing them as "seconds if they were UTC" and
    // subtracting the true epoch yields the local offset (DST included) without non-portable APIs.
    long long lsec = teko_time_days_from_civil(lt.tm_year + 1900, (unsigned)(lt.tm_mon + 1),
                                               (unsigned)lt.tm_mday) * 86400LL
                   + (long long)lt.tm_hour * 3600 + (long long)lt.tm_min * 60 + lt.tm_sec;
    return (long)(lsec - epoch_s);
}
#endif

// `time.now_unix()` -> current Unix epoch seconds as a decimal string (the int arg is ignored).
char* teko_rt_time_now_unix(int ignored)  { (void)ignored; return teko_time_epoch_str(teko_rt_time_now_s()); }
// `time.now_utc()` / `time.now_local()` -> ISO-8601 of "now" in UTC / system-local (DST-correct).
char* teko_rt_time_now_utc(int ignored)   { (void)ignored; return teko_time_format(teko_rt_time_now_s(), 0); }
char* teko_rt_time_now_local(int ignored) {
    (void)ignored; long long e = teko_rt_time_now_s();
    return teko_time_format(e, teko_rt_time_offset(e));
}
// `time.format_utc(epoch)` / `time.format_local(epoch)` -> ISO-8601 of a user epoch (decimal
// string -> the user can do time math on it), UTC / system-local.
char* teko_rt_time_format_utc(const char* epoch_str)   { return teko_time_format(teko_time_parse(epoch_str), 0); }
char* teko_rt_time_format_local(const char* epoch_str) {
    long long e = teko_time_parse(epoch_str);
    return teko_time_format(e, teko_rt_time_offset(e));
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

// Phase 15 (15.A) — object model surface wrappers. The OP_OBJ_* opcodes lower to these
// (SysV/AAPCS calls); the teko_object C runtime (src/runtime/teko_object.c) is the source of
// truth. The handle is the TekoObject* carried through the surface as a register-width integer;
// field cells are register-width (a field may hold another object's handle). Available on EVERY
// target — native AND the wasm32 reactor (so this MUST sit outside the `!__wasm__` CSPRNG tail).
long teko_rt_object_new(long nfields) {
    return (long)(intptr_t)teko_object_new((int)nfields);
}
long teko_rt_object_set(long handle, long idx, long value) {
    teko_object_set((TekoObject*)(intptr_t)handle, (int)idx, value);
    return 0;
}
long teko_rt_object_get(long handle, long idx) {
    return teko_object_get((TekoObject*)(intptr_t)handle, (int)idx);
}
long teko_rt_object_free(long handle) {
    teko_object_free((TekoObject*)(intptr_t)handle);
    return 0;
}

// Phase 15 (15.B) — static vtable surface wrappers. The OP_VTABLE_* opcodes lower to these; the
// teko_vtable C runtime is the source of truth (compile-time-populated dispatch table). Available
// on every target (native + the wasm32 reactor). Pure register-width integers (type/method/slot).
long teko_rt_vtable_set(long type_id, long method_id, long slot) {
    teko_vtable_set(type_id, method_id, slot);
    return 0;
}
long teko_rt_vtable_get(long type_id, long method_id) {
    return teko_vtable_get(type_id, method_id);
}

// Phase 16 (Casting / Type Conversions & Parsing) — culture-invariant conversion surface.
// teko_convert.c is the source of truth (linked natively, compiled into the wasm32 reactor). The
// value-carrying params are i32 to match the accumulator/reactor ABI; the full i64 core is KAT'd.
char* teko_rt_int_to_string(int v)  { return teko_convert_i64_to_string((long long)v); }
// Phase 17.D — float->string (id 50). The 17.C Ryu shortest-round-trip formatter is the source of
// truth (src/runtime/teko_convert_f64.c, linked natively, compiled into the wasm32 reactor). This
// is the ONLY conversion wrapper whose value param is a `double` (the f64-arg ABI; see teko_rt.h).
char* teko_rt_float_to_string(double v) { return teko_convert_f64_to_string(v); }
char* teko_rt_bool_to_string(int v) { return teko_convert_bool_to_string(v); }
char* teko_rt_str_concat(const char* a, const char* b) { return teko_convert_str_concat(a, b); }
// Phase 16.E — explicit integer formats.
char* teko_rt_to_radix(int v, int radix) { return teko_convert_i64_to_radix((long long)v, radix); }
char* teko_rt_pad(int v, int width)      { return teko_convert_i64_pad((long long)v, width); }
char* teko_rt_group(int v)               { return teko_convert_i64_grouped((long long)v, ','); }

// Phase 16.F — CHECKED parse (fail-loud). A malformed/overflowing input does NOT silently return 0
// (no silent truncation/UB): it aborts the program. Native prints a diagnostic to stderr and exits
// non-zero; the wasm32 reactor traps (`unreachable` -> a WebAssembly RuntimeError the host sees).
static void teko_rt_die(const char* msg) {
#if defined(__wasm__)
    (void)msg; __builtin_trap();
#else
    fputs("teko: ", stderr); fputs(msg, stderr); fputs("\n", stderr);
    exit(70); // EX_DATAERR
#endif
}
int teko_rt_parse_int(const char* s) {
    long long v;
    if (!teko_convert_parse_i64(s, &v)) teko_rt_die("convert.parse_int: invalid integer");
    return (int)v;
}
int teko_rt_parse_bool(const char* s) {
    int v;
    if (!teko_convert_parse_bool(s, &v)) teko_rt_die("convert.parse_bool: invalid boolean");
    return v;
}
// Phase 17.E — CHECKED string -> f64 (id 54). The INVERSE of id 50's f64-arg ABI: the arg is a
// string ($w0 -> rdi/x0), the result is a `double` returned in xmm0/d0 (= $f0). A malformed or
// out-of-range (±Inf) input FAILS LOUDLY via the SAME teko_rt_die path as parse_int/parse_bool
// (exit 70 + stderr native; __builtin_trap in the wasm reactor). The correctly-rounded freestanding
// parser is the 17.E teko_convert_parse_f64 (src/runtime/teko_convert_f64.c) — the SAME C source
// compiled into the wasm32 reactor, so native and WASM agree bit-for-bit.
double teko_rt_parse_float(const char* s) {
    double v;
    if (!teko_convert_parse_f64(s, &v)) teko_rt_die("convert.parse_float: invalid float");
    return v;
}

// Phase 17 (17.B) — CHECKED float->int (OP_F2I) FAIL-LOUD landing pad. The hosted emitter emits an
// inline NaN + i32-range guard (matched to WASM's i32.trunc_f64_s valid open interval) and `call`s
// this when the float cannot be represented as an i32 — so a value that traps on WASM also aborts
// non-zero here, via the SAME exit-70 + stderr path the 16.F parsers use. No arguments (the guard
// already decided); never returns.
void teko_rt_f2i_fail(void) {
    teko_rt_die("convert.to_int: float out of i32 range or not finite");
}

// Phase 18 (18.E.1) — FIXED-size CONTIGUOUS array surface wrappers (OP_ARR_* lower to these). The
// teko_array C runtime (src/runtime/teko_array.c) is the source of truth. UNLIKE the object store,
// get/set are CHECKED FAIL-LOUD: an out-of-range index aborts via teko_rt_die (the SAME exit-70 +
// stderr path / wasm __builtin_trap the 16.F/17 checked surface uses), NOT a defensive no-op. Placed
// AFTER teko_rt_die (a static) so it is in scope. Available on every target (native + wasm reactor).
long teko_rt_array_new(long n) {
    return (long)(intptr_t)teko_array_new((int)n);
}
long teko_rt_array_get(long handle, long i) {
    intptr_t v = 0;
    if (!teko_array_get((const TekoArray*)(intptr_t)handle, (int)i, &v))
        teko_rt_die("array: index out of bounds");
    return (long)v;
}
long teko_rt_array_set(long handle, long i, long value) {
    if (!teko_array_set((TekoArray*)(intptr_t)handle, (int)i, (intptr_t)value))
        teko_rt_die("array: index out of bounds");
    return 0;
}
long teko_rt_array_len(long handle) {
    return (long)teko_array_len((const TekoArray*)(intptr_t)handle);
}

// Phase 18 (18.E.2) — TYPED `i32[]` PACKED numeric array surface wrappers (OP_IARR_* lower to these).
// Mirrors teko_rt_array_* EXACTLY; the ONLY difference is the cell type (PACKED int32_t — the SIMD
// substrate). Values are i32-range ints carried in `long` (truncated to int32 on store). get/set are
// CHECKED FAIL-LOUD: an out-of-range index aborts via teko_rt_die (the SAME exit-70 + stderr path the
// array surface uses), NOT a defensive no-op.
long teko_rt_iarray_new(long n) {
    return (long)(intptr_t)teko_iarray_new((int)n);
}
long teko_rt_iarray_get(long handle, long i) {
    int32_t v = 0;
    if (!teko_iarray_get((const TekoIArray*)(intptr_t)handle, (int)i, &v))
        teko_rt_die("iarray: index out of bounds");
    return (long)v;
}
long teko_rt_iarray_set(long handle, long i, long value) {
    if (!teko_iarray_set((TekoIArray*)(intptr_t)handle, (int)i, (int32_t)value))
        teko_rt_die("iarray: index out of bounds");
    return 0;
}
long teko_rt_iarray_len(long handle) {
    return (long)teko_iarray_len((const TekoIArray*)(intptr_t)handle);
}
// Phase 18 (18.E.4) — SIMD substrate access. data returns the packed int32 cell-buffer pointer as a
// register-width integer (the run the BACKEND-emitted teko_simd_sum_i32 kernel walks); sum is the
// SCALAR reference reduction (the self-check oracle + the honest fallback for non-vector targets).
long teko_rt_iarray_data(long handle) {
    return (long)(intptr_t)teko_iarray_data((TekoIArray*)(intptr_t)handle);
}
long teko_rt_iarray_sum(long handle) {
    return (long)teko_iarray_sum((const TekoIArray*)(intptr_t)handle);
}

// Phase 17.F.3 — the 256-byte `decimal` VALUE-MODEL runtime wrappers. The opcode family
// (OP_DADD/DSUB/DMUL/DDIV/DMOD/DEQ..DGE) lowers to these; the args are ALWAYS pointers into a
// 256-byte decimal slot (native stack/frame, WASM linear memory — the same shared-memory pointer
// ABI as the crypto hex strings). Each wraps the 17.F.1 core and FAILS LOUD on the core's 0 return
// (overflow / divide-by-zero) via teko_rt_die — native exit 70 + stderr, wasm32 reactor
// __builtin_trap — exactly the 16.F checked-parser posture (no NaN/Inf; decimal is always finite).
void teko_rt_decimal_add(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    if (!teko_decimal_add(a, b, out)) teko_rt_die("decimal: add overflow");
}
void teko_rt_decimal_sub(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    if (!teko_decimal_sub(a, b, out)) teko_rt_die("decimal: sub overflow");
}
void teko_rt_decimal_mul(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    if (!teko_decimal_mul(a, b, out)) teko_rt_die("decimal: mul overflow");
}
void teko_rt_decimal_div(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    if (!teko_decimal_div(a, b, out)) teko_rt_die("decimal: divide by zero or overflow");
}
void teko_rt_decimal_mod(const teko_decimal* a, const teko_decimal* b, teko_decimal* out) {
    if (!teko_decimal_mod(a, b, out)) teko_rt_die("decimal: modulo by zero or overflow");
}
// Compare: writes -1 / 0 / +1 to *out_lt_eq_gt (the core cannot fail — always-finite operands).
// The emitter maps that tri-state to the i32 0/1 boolean each OP_DEQ..DGE wants in $w0.
void teko_rt_decimal_cmp(const teko_decimal* a, const teko_decimal* b, int* out_lt_eq_gt) {
    (void)teko_decimal_cmp(a, b, out_lt_eq_gt);
}

// Phase 17.F.4 — checked int/float ↔ decimal CAST wrappers. F2D/D2F bridge through the
// shortest-string form (teko_convert_f64_to_string / teko_convert_parse_f64) — both sides are
// correctly-rounded, so the cast is clean — reusing the 17.C/17.E cores rather than a fresh path.
void teko_rt_decimal_from_i32(int v, teko_decimal* out) {
    (void)teko_decimal_from_i32(v, out); // cannot fail (|v| fits well within 1984 bits)
}
void teko_rt_decimal_from_f64(double v, teko_decimal* out) {
    // f64 -> shortest `.`-decimal -> exact decimal. Non-finite (NaN/Inf) has no finite decimal
    // form and the shortest-string renderer yields "NaN"/"Infinity", which teko_decimal_parse
    // rejects -> fail-loud (decimal is always finite). A normal finite double round-trips exactly.
    char* s = teko_convert_f64_to_string(v);
    if (!s) { teko_rt_die("convert.to_decimal: out of memory"); return; }
    int ok = teko_decimal_parse(s, out);
    free(s);
    if (!ok) teko_rt_die("convert.to_decimal: value not representable as a finite decimal");
}
int teko_rt_decimal_to_i32(const teko_decimal* d) {
    int v;
    if (!teko_decimal_to_i32(d, &v)) teko_rt_die("convert.to_int: decimal out of i32 range");
    return v;
}
double teko_rt_decimal_to_f64(const teko_decimal* d) {
    // decimal -> exact plain `.`-decimal string -> correctly-rounded f64 (round-to-nearest-even).
    char* s = teko_decimal_to_string(d);
    if (!s) { teko_rt_die("convert.to_float: out of memory"); return 0.0; }
    double v = 0.0;
    int ok = teko_convert_parse_f64(s, &v);
    free(s);
    // The plain-decimal string is always a valid finite number; a parse failure would only be a
    // magnitude beyond DBL_MAX (overflow to ±Inf). Fail loud rather than return Inf silently.
    if (!ok) teko_rt_die("convert.to_float: decimal magnitude overflows f64");
    return v;
}

// Phase 17.F.4 — decimal language surface (ids 59/60).
char* teko_rt_decimal_to_string(const teko_decimal* d) {
    char* s = teko_decimal_to_string(d);
    if (!s) teko_rt_die("decimal.to_string: out of memory");
    return s;
}
void teko_rt_decimal_parse(const char* s, teko_decimal* out) {
    if (!teko_decimal_parse(s, out)) teko_rt_die("decimal.parse: invalid decimal");
}
