// Native UUID / GUID (RFC 4122 / RFC 9562). Portable C23, no external libraries.

#include "teko_uuid.h"
#include "teko_crypto_md5.h"
#include "teko_crypto_sha1.h"
#include "teko_crypto_random.h"

#include <string.h>

const uint8_t TEKO_UUID_NS_DNS[16] = {
    0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1, 0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
};
const uint8_t TEKO_UUID_NS_URL[16] = {
    0x6b, 0xa7, 0xb8, 0x11, 0x9d, 0xad, 0x11, 0xd1, 0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
};
const uint8_t TEKO_UUID_NS_OID[16] = {
    0x6b, 0xa7, 0xb8, 0x12, 0x9d, 0xad, 0x11, 0xd1, 0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
};
const uint8_t TEKO_UUID_NS_X500[16] = {
    0x6b, 0xa7, 0xb8, 0x14, 0x9d, 0xad, 0x11, 0xd1, 0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
};

// Stamp the 4-bit version (high nibble of byte 6) and the 10xx RFC-4122 variant (byte 8).
static void teko_uuid_stamp(uint8_t out[16], unsigned version) {
    out[6] = (uint8_t)((out[6] & 0x0fu) | (uint8_t)(version << 4));
    out[8] = (uint8_t)((out[8] & 0x3fu) | 0x80u);
}

void teko_uuid_nil(uint8_t out[16]) {
    memset(out, 0, 16u);
}

void teko_uuid_v3(uint8_t out[16], const uint8_t ns[16], const uint8_t* name, size_t name_len) {
    TekoMd5Ctx ctx;
    uint8_t digest[TEKO_MD5_DIGEST_LEN];
    teko_md5_init(&ctx);
    teko_md5_update(&ctx, ns, 16u);
    teko_md5_update(&ctx, name, name_len);
    teko_md5_final(&ctx, digest);
    memcpy(out, digest, 16u);
    teko_uuid_stamp(out, 3u);
}

void teko_uuid_v5(uint8_t out[16], const uint8_t ns[16], const uint8_t* name, size_t name_len) {
    TekoSha1Ctx ctx;
    uint8_t digest[TEKO_SHA1_DIGEST_LEN];
    teko_sha1_init(&ctx);
    teko_sha1_update(&ctx, ns, 16u);
    teko_sha1_update(&ctx, name, name_len);
    teko_sha1_final(&ctx, digest);
    memcpy(out, digest, 16u); // truncate the 20-byte digest to 16
    teko_uuid_stamp(out, 5u);
}

int teko_uuid_v4(uint8_t out[16]) {
    if (teko_csprng_bytes(out, 16u) != 0) return -1;
    teko_uuid_stamp(out, 4u);
    return 0;
}

int teko_uuid_v7(uint8_t out[16], uint64_t unix_ms) {
    unsigned i;
    // 48-bit big-endian timestamp in bytes 0..5.
    for (i = 0u; i < 6u; ++i) {
        out[i] = (uint8_t)(unix_ms >> (40u - 8u * i));
    }
    if (teko_csprng_bytes(out + 6, 10u) != 0) return -1;
    teko_uuid_stamp(out, 7u);
    return 0;
}

void teko_uuid_v8(uint8_t out[16], const uint8_t data[16]) {
    memcpy(out, data, 16u);
    teko_uuid_stamp(out, 8u); // version 8 + 10xx variant over the custom bits
}

void teko_uuid_format(char out[36], const uint8_t uuid[16]) {
    static const char* HEX = "0123456789abcdef";
    static const int dash_after[4] = {3, 5, 7, 9}; // byte indices after which a '-' goes
    unsigned i, pos = 0u, d = 0u;
    for (i = 0u; i < 16u; ++i) {
        out[pos++] = HEX[(uuid[i] >> 4) & 0xFu];
        out[pos++] = HEX[uuid[i] & 0xFu];
        if (d < 4u && i == (unsigned)dash_after[d]) {
            out[pos++] = '-';
            d++;
        }
    }
}

static int teko_uuid_hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int teko_uuid_parse(uint8_t out[16], const char* str) {
    unsigned i, b = 0u;
    if (!str) return -1;
    for (i = 0u; i < 36u; ++i) {
        if (i == 8u || i == 13u || i == 18u || i == 23u) {
            if (str[i] != '-') return -1;
        } else {
            int hi, lo;
            hi = teko_uuid_hexval(str[i]);
            lo = teko_uuid_hexval(str[i + 1u]);
            if (hi < 0 || lo < 0) return -1;
            out[b++] = (uint8_t)((hi << 4) | lo);
            i++; // consumed two hex chars
        }
    }
    if (str[36] != '\0') return -1; // reject trailing junk
    return (b == 16u) ? 0 : -1;
}

int teko_uuid_version(const uint8_t uuid[16]) {
    return (uuid[6] >> 4) & 0x0f;
}

int teko_uuid_is_rfc4122_variant(const uint8_t uuid[16]) {
    return ((uuid[8] & 0xc0u) == 0x80u) ? 1 : 0;
}
