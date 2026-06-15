// Native PBKDF2 (RFC 2898). Generic over HMAC-SHA-256/512; portable C23, no external libs.
//
//   T_i = U_1 ^ U_2 ^ ... ^ U_c, where
//   U_1 = HMAC(P, S || INT32_BE(i)),  U_j = HMAC(P, U_{j-1})
//   DK  = T_1 || T_2 || ... truncated to dk_len.

#include "teko_crypto_pbkdf2.h"
#include "teko_crypto_hmac.h"

#include <stdlib.h>
#include <string.h>

#define TEKO_PBKDF2_MAX_HASH 64u

static void teko_pbkdf2_hmac(size_t hash_len,
                             const uint8_t* key, size_t key_len,
                             const uint8_t* msg, size_t msg_len,
                             uint8_t* out) {
    if (hash_len == 32u) {
        teko_hmac_sha256(key, key_len, msg, msg_len, out);
    } else {
        teko_hmac_sha512(key, key_len, msg, msg_len, out);
    }
}

static int teko_pbkdf2(size_t hash_len,
                       const uint8_t* password, size_t password_len,
                       const uint8_t* salt, size_t salt_len,
                       uint32_t iterations,
                       uint8_t* dk, size_t dk_len) {
    uint8_t t[TEKO_PBKDF2_MAX_HASH];
    uint8_t u[TEKO_PBKDF2_MAX_HASH];
    uint8_t* salt_block;
    size_t blocks, i;
    uint32_t block_index;

    if (iterations == 0u) return -1;
    if (dk_len == 0u) return 0;

    // Staging buffer for S || INT32_BE(block_index).
    salt_block = (uint8_t*)malloc(salt_len + 4u);
    if (!salt_block) return -1;
    if (salt_len > 0u) memcpy(salt_block, salt, salt_len);

    blocks = (dk_len + hash_len - 1u) / hash_len;
    for (block_index = 1u, i = 0u; i < blocks; ++i, ++block_index) {
        uint32_t j;
        size_t k, off, take;

        salt_block[salt_len + 0u] = (uint8_t)(block_index >> 24);
        salt_block[salt_len + 1u] = (uint8_t)(block_index >> 16);
        salt_block[salt_len + 2u] = (uint8_t)(block_index >> 8);
        salt_block[salt_len + 3u] = (uint8_t)(block_index);

        // U_1, then T = U_1.
        teko_pbkdf2_hmac(hash_len, password, password_len, salt_block, salt_len + 4u, u);
        memcpy(t, u, hash_len);

        // U_2..U_c, folding each into T.
        for (j = 1u; j < iterations; ++j) {
            teko_pbkdf2_hmac(hash_len, password, password_len, u, hash_len, u);
            for (k = 0u; k < hash_len; ++k) {
                t[k] ^= u[k];
            }
        }

        off = i * hash_len;
        take = (dk_len - off < hash_len) ? (dk_len - off) : hash_len;
        memcpy(dk + off, t, take);
    }

    free(salt_block);
    return 0;
}

int teko_pbkdf2_hmac_sha256(const uint8_t* password, size_t password_len,
                            const uint8_t* salt, size_t salt_len,
                            uint32_t iterations,
                            uint8_t* dk, size_t dk_len) {
    return teko_pbkdf2(32u, password, password_len, salt, salt_len, iterations, dk, dk_len);
}

int teko_pbkdf2_hmac_sha512(const uint8_t* password, size_t password_len,
                            const uint8_t* salt, size_t salt_len,
                            uint32_t iterations,
                            uint8_t* dk, size_t dk_len) {
    return teko_pbkdf2(64u, password, password_len, salt, salt_len, iterations, dk, dk_len);
}
