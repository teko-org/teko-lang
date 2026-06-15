#include "unity.h"
#include "../../src/runtime/teko_crypto_random.h"

#include <string.h>

// A CSPRNG has no known-answer test (output is non-deterministic). These exercise the
// wrapper's contract: it succeeds, fills the whole buffer, produces distinct outputs,
// and yields a plausible bit distribution (sanity, not a statistical certification).
void test_teko_crypto_csprng_fills_and_differs(void) {
    uint8_t a[64];
    uint8_t b[64];

    // Zero-length is a no-op success.
    TEST_ASSERT_EQUAL_INT(0, teko_csprng_bytes(a, 0u));

    // A canary byte past nothing: fill must write every requested byte.
    memset(a, 0xAB, sizeof(a));
    memset(b, 0xAB, sizeof(b));
    TEST_ASSERT_EQUAL_INT(0, teko_csprng_bytes(a, sizeof(a)));
    TEST_ASSERT_EQUAL_INT(0, teko_csprng_bytes(b, sizeof(b)));

    // Two independent draws must differ (collision probability ~2^-512).
    TEST_ASSERT_NOT_EQUAL(0, memcmp(a, b, sizeof(a)));

    // Not left all-zero and not left as the 0xAB canary across the whole buffer.
    {
        int all_zero = 1, all_canary = 1, i;
        for (i = 0; i < (int)sizeof(a); ++i) {
            if (a[i] != 0x00u) all_zero = 0;
            if (a[i] != 0xABu) all_canary = 0;
        }
        TEST_ASSERT_FALSE(all_zero);
        TEST_ASSERT_FALSE(all_canary);
    }
}

// Over a larger sample, both bit values must appear and no single byte value should
// dominate — a coarse smoke test that the source is not stuck/biased-to-constant.
void test_teko_crypto_csprng_distribution_sanity(void) {
    uint8_t buf[4096];
    size_t ones = 0u, i, bit;
    int max_count = 0, j;
    int counts[256];

    TEST_ASSERT_EQUAL_INT(0, teko_csprng_bytes(buf, sizeof(buf)));

    memset(counts, 0, sizeof(counts));
    for (i = 0u; i < sizeof(buf); ++i) {
        counts[buf[i]]++;
        for (bit = 0u; bit < 8u; ++bit) {
            if (buf[i] & (1u << bit)) ones++;
        }
    }

    // ~50% ones expected over 32768 bits; allow a very wide band (35%..65%).
    TEST_ASSERT_TRUE(ones > (32768u * 35u) / 100u);
    TEST_ASSERT_TRUE(ones < (32768u * 65u) / 100u);

    // No byte value should occupy more than ~12% of 4096 samples (expected ~16).
    for (j = 0; j < 256; ++j) {
        if (counts[j] > max_count) max_count = counts[j];
    }
    TEST_ASSERT_TRUE(max_count < 512);
}
