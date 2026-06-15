#ifndef TEKO_CRYPTO_BLAKE3_H
#define TEKO_CRYPTO_BLAKE3_H

// Native BLAKE3 (default hash mode). Pure, portable C23 — no external libraries.
// Implements the chunk/parent tree + CV stack from the BLAKE3 reference design.
// KAT-tested against the official test vectors in tests/runtime/test_crypto_blake3.c.

#include <stdint.h>
#include <stddef.h>

#define TEKO_BLAKE3_OUT_LEN   32u
#define TEKO_BLAKE3_BLOCK_LEN 64u
#define TEKO_BLAKE3_CHUNK_LEN 1024u

typedef struct {
    uint32_t chaining_value[8];
    uint64_t chunk_counter;
    uint8_t  block[TEKO_BLAKE3_BLOCK_LEN];
    uint8_t  block_len;
    uint8_t  blocks_compressed;
    uint32_t flags;
} TekoBlake3ChunkState;

typedef struct {
    TekoBlake3ChunkState chunk_state;
    uint32_t key[8];
    uint32_t cv_stack[54][8]; // enough for the maximum tree depth
    uint8_t  cv_stack_len;
    uint32_t flags;
} TekoBlake3Ctx;

// Streaming API: init -> update* -> finalize (writes out_len bytes; XOF-capable).
void teko_blake3_init(TekoBlake3Ctx* ctx);
void teko_blake3_update(TekoBlake3Ctx* ctx, const uint8_t* data, size_t len);
void teko_blake3_finalize(TekoBlake3Ctx* ctx, uint8_t* out, size_t out_len);

// One-shot convenience: 32-byte digest.
void teko_blake3(const uint8_t* data, size_t len, uint8_t out[TEKO_BLAKE3_OUT_LEN]);

#endif // TEKO_CRYPTO_BLAKE3_H
