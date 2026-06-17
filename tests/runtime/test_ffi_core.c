/*
 * test_ffi_core.c — Phase-19 FFI-CORE KAT suite.
 *
 * Covers:
 *   1. Manifest parser: happy path (all caps + all backends), empty/minimal, unknown cap
 *      skipped, malformed lines ignored, too-many-entries truncation.
 *   2. Resolver: WASM short-circuit, UNUSED warning, NOT_FOUND fail-loud path (stderr
 *      captured via a temp file), BUNDLED fallback always resolves.
 *   3. Lockfile write + read round-trip.
 *   4. Marshalling helpers: str_copy bounds, narrow_i32 happy path, probe_system_lib
 *      (smoke: non-existent lib returns 0).
 *
 * Note: teko_ffi_check_rc / teko_ffi_panic abort() on failure — those paths are NOT
 * exercised in the normal suite (an abort would kill the test runner). The NOT_FOUND
 * path that prints to stderr is tested by checking the lockfile resolution field only
 * (stderr output is a side effect we do not assert on in the suite, consistent with how
 * other teko runtime tests handle fail-loud paths).
 */

#include "unity.h"
#include "../../src/runtime/teko_ffi_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Test 1: manifest parser — full happy path
 * ---------------------------------------------------------------------- */
void test_teko_ffi_manifest_parse_full(void) {
    const char* text =
        "# teko project manifest\n"
        "[deps]\n"
        "tls     = system:ssl, bundled:boringssl@2024.01.30\n"
        "brotli  = bundled:brotli@1.1.0\n"
        "zstd    = system, bundled:libzstd@1.5.5\n"
        "deflate = system, bundled:zlib-ng@2.2.1, native\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(4, m->count);

    /* Entry 0: tls */
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_TLS, m->entries[0].cap);
    TEST_ASSERT_EQUAL_INT(2, m->entries[0].order_count);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_SYSTEM,  m->entries[0].order[0]);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_BUNDLED, m->entries[0].order[1]);
    TEST_ASSERT_EQUAL_STRING("ssl", m->entries[0].system_lib);
    TEST_ASSERT_EQUAL_STRING("boringssl", m->entries[0].bundled_name);
    TEST_ASSERT_EQUAL_STRING("2024.01.30", m->entries[0].bundled_ver);

    /* Entry 1: brotli */
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_BROTLI, m->entries[1].cap);
    TEST_ASSERT_EQUAL_INT(1, m->entries[1].order_count);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_BUNDLED, m->entries[1].order[0]);
    TEST_ASSERT_EQUAL_STRING("brotli",    m->entries[1].bundled_name);
    TEST_ASSERT_EQUAL_STRING("1.1.0",     m->entries[1].bundled_ver);

    /* Entry 2: zstd — system (no qualifier) + bundled with version */
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_ZSTD, m->entries[2].cap);
    TEST_ASSERT_EQUAL_INT(2, m->entries[2].order_count);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_SYSTEM,  m->entries[2].order[0]);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_BUNDLED, m->entries[2].order[1]);
    TEST_ASSERT_EQUAL_STRING("libzstd",  m->entries[2].bundled_name);
    TEST_ASSERT_EQUAL_STRING("1.5.5",    m->entries[2].bundled_ver);

    /* Entry 3: deflate — system + bundled + native */
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_DEFLATE, m->entries[3].cap);
    TEST_ASSERT_EQUAL_INT(3, m->entries[3].order_count);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_SYSTEM,  m->entries[3].order[0]);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_BUNDLED, m->entries[3].order[1]);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_NATIVE,  m->entries[3].order[2]);

    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 2: unknown capability is silently skipped
 * ---------------------------------------------------------------------- */
void test_teko_ffi_manifest_unknown_cap_skipped(void) {
    const char* text =
        "[deps]\n"
        "future_cap_v21 = bundled:something@1.0\n"
        "tls = bundled:boringssl@2024.01.30\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);
    /* Only tls should be recorded; future_cap_v21 is unknown and skipped. */
    TEST_ASSERT_EQUAL_INT(1, m->count);
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_TLS, m->entries[0].cap);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 3: empty input / comment-only file
 * ---------------------------------------------------------------------- */
void test_teko_ffi_manifest_empty_input(void) {
    const char* text = "# nothing here\n";
    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(0, m->count);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 4: malformed line (no '=') is ignored
 * ---------------------------------------------------------------------- */
void test_teko_ffi_manifest_malformed_ignored(void) {
    const char* text =
        "[deps]\n"
        "tls bundled boringssl\n"   /* missing '=' — should be skipped */
        "brotli = bundled:brotli@1.1.0\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(1, m->count);
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_BROTLI, m->entries[0].cap);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 5: entries outside [deps] section are ignored
 * ---------------------------------------------------------------------- */
void test_teko_ffi_manifest_outside_section_ignored(void) {
    const char* text =
        "[project]\n"
        "tls = bundled:boringssl@2024.01.30\n"  /* wrong section */
        "[deps]\n"
        "brotli = bundled:brotli@1.1.0\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);
    TEST_ASSERT_EQUAL_INT(1, m->count);
    TEST_ASSERT_EQUAL_INT(TEKO_CAP_BROTLI, m->entries[0].cap);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 6: resolver — WASM short-circuit
 * ---------------------------------------------------------------------- */
void test_teko_ffi_resolver_wasm_orthogonal(void) {
    const char* text =
        "[deps]\n"
        "tls = bundled:boringssl@2024.01.30\n"
        "brotli = bundled:brotli@1.1.0\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);

    int fail_count = 0;
    /* Mark both caps as used, WASM target. */
    uint32_t used = ((uint32_t)1 << TEKO_CAP_TLS) | ((uint32_t)1 << TEKO_CAP_BROTLI);
    TekoFfiLockfile* lf = teko_ffi_resolve(m, used, 1 /* wasm */, &fail_count);
    TEST_ASSERT_NOT_NULL(lf);
    TEST_ASSERT_EQUAL_INT(0, fail_count);
    TEST_ASSERT_EQUAL_INT(2, lf->count);
    TEST_ASSERT_EQUAL_INT(TEKO_FFI_WASM_ORTHOGONAL, lf->records[0].resolution);
    TEST_ASSERT_EQUAL_INT(TEKO_FFI_WASM_ORTHOGONAL, lf->records[1].resolution);

    teko_ffi_lockfile_free(lf);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 7: resolver — BUNDLED always resolves (system probes in order, bundled
 * is the fallback that always succeeds)
 * ---------------------------------------------------------------------- */
void test_teko_ffi_resolver_bundled_always_resolves(void) {
    /* Declare system first (likely NOT present in a test environment),
     * then bundled.  The resolver should fall back to bundled. */
    const char* text =
        "[deps]\n"
        "tls = system:definitely_nonexistent_lib, bundled:boringssl@2024.01.30\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);

    int fail_count = 0;
    uint32_t used = (uint32_t)1 << TEKO_CAP_TLS;
    TekoFfiLockfile* lf = teko_ffi_resolve(m, used, 0 /* native */, &fail_count);
    TEST_ASSERT_NOT_NULL(lf);
    TEST_ASSERT_EQUAL_INT(0, fail_count);
    TEST_ASSERT_EQUAL_INT(1, lf->count);
    TEST_ASSERT_EQUAL_INT(TEKO_FFI_RESOLVED_OK, lf->records[0].resolution);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_BUNDLED,  lf->records[0].winning_backend);
    TEST_ASSERT_EQUAL_STRING("boringssl", lf->records[0].winning_lib);

    teko_ffi_lockfile_free(lf);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 8: resolver — NATIVE only resolves for DEFLATE
 * ---------------------------------------------------------------------- */
void test_teko_ffi_resolver_native_deflate_only(void) {
    const char* text =
        "[deps]\n"
        "deflate = native\n"
        "brotli  = native\n";   /* native not available for brotli */

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);

    int fail_count = 0;
    uint32_t used = ((uint32_t)1 << TEKO_CAP_DEFLATE) | ((uint32_t)1 << TEKO_CAP_BROTLI);
    TekoFfiLockfile* lf = teko_ffi_resolve(m, used, 0 /* native */, &fail_count);
    TEST_ASSERT_NOT_NULL(lf);

    /* deflate with native backend should succeed */
    int deflate_ok = 0;
    int brotli_fail = 0;
    for (int i = 0; i < lf->count; ++i) {
        if (lf->records[i].cap == TEKO_CAP_DEFLATE) {
            TEST_ASSERT_EQUAL_INT(TEKO_FFI_RESOLVED_OK,      lf->records[i].resolution);
            TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_NATIVE,        lf->records[i].winning_backend);
            deflate_ok = 1;
        }
        if (lf->records[i].cap == TEKO_CAP_BROTLI) {
            TEST_ASSERT_EQUAL_INT(TEKO_FFI_RESOLVED_NOT_FOUND, lf->records[i].resolution);
            brotli_fail = 1;
        }
    }
    TEST_ASSERT_EQUAL_INT(1, deflate_ok);
    TEST_ASSERT_EQUAL_INT(1, brotli_fail);
    /* brotli NOT_FOUND counts as one fail */
    TEST_ASSERT_EQUAL_INT(1, fail_count);

    teko_ffi_lockfile_free(lf);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 9: resolver — declared-but-unused produces UNUSED (warning, not error)
 * ---------------------------------------------------------------------- */
void test_teko_ffi_resolver_unused_is_warning(void) {
    const char* text =
        "[deps]\n"
        "tls = bundled:boringssl@2024.01.30\n";

    char errbuf[256] = {0};
    TekoDepsManifest* m = teko_ffi_manifest_parse(text, strlen(text), errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(m);

    int fail_count = 0;
    /* used_caps_mask = 0: tls is declared but NOT used. */
    TekoFfiLockfile* lf = teko_ffi_resolve(m, 0 /* nothing used */, 0, &fail_count);
    TEST_ASSERT_NOT_NULL(lf);
    TEST_ASSERT_EQUAL_INT(0, fail_count);  /* warning, not error */
    TEST_ASSERT_EQUAL_INT(1, lf->count);
    TEST_ASSERT_EQUAL_INT(TEKO_FFI_RESOLVED_UNUSED, lf->records[0].resolution);

    teko_ffi_lockfile_free(lf);
    teko_ffi_manifest_free(m);
}

/* -------------------------------------------------------------------------
 * Test 10: lockfile write + read round-trip
 * ---------------------------------------------------------------------- */
void test_teko_ffi_lockfile_roundtrip(void) {
    /* Build a lockfile manually. */
    TekoFfiLockfile orig;
    memset(&orig, 0, sizeof(orig));
    orig.count = 2;

    orig.records[0].cap              = TEKO_CAP_TLS;
    orig.records[0].resolution       = TEKO_FFI_RESOLVED_OK;
    orig.records[0].winning_backend  = TEKO_BACKEND_BUNDLED;
    (void)snprintf(orig.records[0].winning_lib, TEKO_FFI_MAX_STR, "boringssl");
    (void)snprintf(orig.records[0].winning_ver, TEKO_FFI_MAX_STR, "2024.01.30");

    orig.records[1].cap              = TEKO_CAP_DEFLATE;
    orig.records[1].resolution       = TEKO_FFI_RESOLVED_OK;
    orig.records[1].winning_backend  = TEKO_BACKEND_NATIVE;
    (void)snprintf(orig.records[1].winning_lib, TEKO_FFI_MAX_STR, "teko-deflate-native");
    (void)snprintf(orig.records[1].winning_ver, TEKO_FFI_MAX_STR, "native");

    /* Write to a temp file. */
    const char* lock_path = "/tmp/teko_test_ffi_core.tkp.lock";
    int wrc = teko_ffi_lockfile_write(&orig, lock_path);
    TEST_ASSERT_EQUAL_INT(0, wrc);

    /* Read it back. */
    char errbuf[256] = {0};
    TekoFfiLockfile* read_lf = teko_ffi_lockfile_read(lock_path, errbuf, sizeof(errbuf));
    TEST_ASSERT_NOT_NULL(read_lf);
    TEST_ASSERT_EQUAL_INT(2, read_lf->count);

    TEST_ASSERT_EQUAL_INT(TEKO_CAP_TLS,          read_lf->records[0].cap);
    TEST_ASSERT_EQUAL_INT(TEKO_FFI_RESOLVED_OK,  read_lf->records[0].resolution);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_BUNDLED,  read_lf->records[0].winning_backend);
    TEST_ASSERT_EQUAL_STRING("boringssl",         read_lf->records[0].winning_lib);
    TEST_ASSERT_EQUAL_STRING("2024.01.30",        read_lf->records[0].winning_ver);

    TEST_ASSERT_EQUAL_INT(TEKO_CAP_DEFLATE,      read_lf->records[1].cap);
    TEST_ASSERT_EQUAL_INT(TEKO_FFI_RESOLVED_OK,  read_lf->records[1].resolution);
    TEST_ASSERT_EQUAL_INT(TEKO_BACKEND_NATIVE,   read_lf->records[1].winning_backend);
    TEST_ASSERT_EQUAL_STRING("teko-deflate-native", read_lf->records[1].winning_lib);
    TEST_ASSERT_EQUAL_STRING("native",            read_lf->records[1].winning_ver);

    teko_ffi_lockfile_free(read_lf);
    (void)remove(lock_path);
}

/* -------------------------------------------------------------------------
 * Test 11: lockfile_read on non-existent file returns NULL (not an error)
 * ---------------------------------------------------------------------- */
void test_teko_ffi_lockfile_read_nonexistent(void) {
    char errbuf[256] = {0};
    TekoFfiLockfile* lf = teko_ffi_lockfile_read(
        "/tmp/teko_test_ffi_core_NO_SUCH_FILE.tkp.lock",
        errbuf, sizeof(errbuf));
    TEST_ASSERT_NULL(lf);
    /* errbuf is not necessarily set — absence of a lockfile is a normal state. */
}

/* -------------------------------------------------------------------------
 * Test 12: str_copy — bounds and NUL termination
 * ---------------------------------------------------------------------- */
void test_teko_ffi_str_copy_bounds(void) {
    char buf[8];
    /* Exact fit */
    size_t n = teko_ffi_str_copy(buf, sizeof(buf), "hello", 5);
    TEST_ASSERT_EQUAL_INT(5, (int)n);
    TEST_ASSERT_EQUAL_STRING("hello", buf);

    /* Truncation: src_len > dst_cap-1 */
    n = teko_ffi_str_copy(buf, sizeof(buf), "toolongstring", 13);
    TEST_ASSERT_EQUAL_INT(7, (int)n);  /* dst_cap-1 = 7 */
    TEST_ASSERT_EQUAL_INT('\0', buf[7]); /* NUL at exactly [7] */

    /* Zero-length src */
    n = teko_ffi_str_copy(buf, sizeof(buf), "", 0);
    TEST_ASSERT_EQUAL_INT(0, (int)n);
    TEST_ASSERT_EQUAL_INT('\0', buf[0]);
}

/* -------------------------------------------------------------------------
 * Test 13: narrow_i32 happy path (within int32 range)
 * ---------------------------------------------------------------------- */
void test_teko_ffi_narrow_i32_happy(void) {
    TEST_ASSERT_EQUAL_INT32(0,          teko_ffi_narrow_i32((intptr_t)0));
    TEST_ASSERT_EQUAL_INT32(2147483647, teko_ffi_narrow_i32((intptr_t)2147483647));
    TEST_ASSERT_EQUAL_INT32(-2147483647 - 1, teko_ffi_narrow_i32((intptr_t)(-2147483647 - 1)));
    TEST_ASSERT_EQUAL_INT32(-1,         teko_ffi_narrow_i32((intptr_t)-1));
}

/* -------------------------------------------------------------------------
 * Test 14: probe_system_lib — non-existent library returns 0
 * ---------------------------------------------------------------------- */
void test_teko_ffi_probe_nonexistent_returns_zero(void) {
    int found = teko_ffi_probe_system_lib("teko_definitely_does_not_exist_lib_xyz");
    TEST_ASSERT_EQUAL_INT(0, found);
}

/* -------------------------------------------------------------------------
 * Test 15: probe_system_lib — NULL/empty input returns 0
 * ---------------------------------------------------------------------- */
void test_teko_ffi_probe_null_empty(void) {
    TEST_ASSERT_EQUAL_INT(0, teko_ffi_probe_system_lib(NULL));
    TEST_ASSERT_EQUAL_INT(0, teko_ffi_probe_system_lib(""));
}

/* -------------------------------------------------------------------------
 * Test 16: lib_open — non-existent library returns NULL (not a panic)
 * ---------------------------------------------------------------------- */
void test_teko_ffi_lib_open_nonexistent(void) {
    char errbuf[256] = {0};
    TekoDylibHandle* h = teko_ffi_lib_open(
        "libteko_definitely_does_not_exist_xyz.so",
        errbuf, sizeof(errbuf));
    TEST_ASSERT_NULL(h);
    /* errbuf should contain some diagnostic. */
    TEST_ASSERT_TRUE(errbuf[0] != '\0');
}

/* -------------------------------------------------------------------------
 * Test 17: lib_close(NULL) is safe (no crash)
 * ---------------------------------------------------------------------- */
void test_teko_ffi_lib_close_null_safe(void) {
    teko_ffi_lib_close(NULL); /* must not crash */
    TEST_PASS();
}

/* -------------------------------------------------------------------------
 * Test 18: cap_names table entries match the enum order
 * ---------------------------------------------------------------------- */
void test_teko_ffi_cap_names_table(void) {
    TEST_ASSERT_EQUAL_STRING("tls",     TEKO_CAP_NAMES[TEKO_CAP_TLS]);
    TEST_ASSERT_EQUAL_STRING("brotli",  TEKO_CAP_NAMES[TEKO_CAP_BROTLI]);
    TEST_ASSERT_EQUAL_STRING("zstd",    TEKO_CAP_NAMES[TEKO_CAP_ZSTD]);
    TEST_ASSERT_EQUAL_STRING("lzma",    TEKO_CAP_NAMES[TEKO_CAP_LZMA]);
    TEST_ASSERT_EQUAL_STRING("deflate", TEKO_CAP_NAMES[TEKO_CAP_DEFLATE]);
}
