// AES-CTR and AES-CBC (NIST SP 800-38A) over the constant-time AES core. Portable C23.

#include "teko_crypto_aes_modes.h"

#include <string.h>

// Increment a 16-byte big-endian counter block in place.
static void teko_aes_ctr_inc(uint8_t ctr[16]) {
    int i;
    for (i = 15; i >= 0; --i) {
        ctr[i] = (uint8_t)(ctr[i] + 1u);
        if (ctr[i] != 0u) break; // no carry out of this byte
    }
}

void teko_aes_ctr_xor(const TekoAesKey* key, const uint8_t iv[16],
                      const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t ctr[16];
    uint8_t ks[16];
    size_t off = 0u;

    memcpy(ctr, iv, 16u);
    while (off < len) {
        size_t i;
        size_t take = (len - off < 16u) ? (len - off) : 16u;
        teko_aes_encrypt_block(key, ctr, ks);
        for (i = 0u; i < take; ++i) {
            out[off + i] = (uint8_t)(in[off + i] ^ ks[i]);
        }
        teko_aes_ctr_inc(ctr);
        off += take;
    }
}

int teko_aes_cbc_encrypt(const TekoAesKey* key, const uint8_t iv[16],
                         const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t prev[16];
    size_t off;

    if (len == 0u || (len % 16u) != 0u) return -1;

    memcpy(prev, iv, 16u);
    for (off = 0u; off < len; off += 16u) {
        uint8_t block[16];
        unsigned i;
        for (i = 0u; i < 16u; ++i) block[i] = (uint8_t)(in[off + i] ^ prev[i]);
        teko_aes_encrypt_block(key, block, out + off);
        memcpy(prev, out + off, 16u); // C_{i-1} for the next block
    }
    return 0;
}

int teko_aes_cbc_decrypt(const TekoAesKey* key, const uint8_t iv[16],
                         const uint8_t* in, uint8_t* out, size_t len) {
    uint8_t prev[16];
    size_t off;

    if (len == 0u || (len % 16u) != 0u) return -1;

    memcpy(prev, iv, 16u);
    for (off = 0u; off < len; off += 16u) {
        uint8_t cipher[16];
        uint8_t plain[16];
        unsigned i;
        memcpy(cipher, in + off, 16u); // save C_i before out may alias in
        teko_aes_decrypt_block(key, cipher, plain);
        for (i = 0u; i < 16u; ++i) out[off + i] = (uint8_t)(plain[i] ^ prev[i]);
        memcpy(prev, cipher, 16u);
    }
    return 0;
}
