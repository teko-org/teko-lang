// Native BLAKE3, default hash mode (BLAKE3 reference design). Portable C23, no
// external libraries. Chunk chaining + binary tree of parent nodes via a CV stack.

#include "teko_crypto_blake3.h"

#include <string.h>

#define TEKO_B3_CHUNK_START (1u << 0)
#define TEKO_B3_CHUNK_END   (1u << 1)
#define TEKO_B3_PARENT      (1u << 2)
#define TEKO_B3_ROOT        (1u << 3)

static const uint32_t TEKO_B3_IV[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};

static const uint8_t TEKO_B3_MSG_PERM[16] = {
    2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8
};

static inline uint32_t teko_b3_rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32u - n));
}

static inline void teko_b3_g(uint32_t s[16], int a, int b, int c, int d, uint32_t mx, uint32_t my) {
    s[a] = s[a] + s[b] + mx;
    s[d] = teko_b3_rotr(s[d] ^ s[a], 16);
    s[c] = s[c] + s[d];
    s[b] = teko_b3_rotr(s[b] ^ s[c], 12);
    s[a] = s[a] + s[b] + my;
    s[d] = teko_b3_rotr(s[d] ^ s[a], 8);
    s[c] = s[c] + s[d];
    s[b] = teko_b3_rotr(s[b] ^ s[c], 7);
}

static void teko_b3_round(uint32_t s[16], const uint32_t m[16]) {
    // Columns.
    teko_b3_g(s, 0, 4, 8, 12, m[0], m[1]);
    teko_b3_g(s, 1, 5, 9, 13, m[2], m[3]);
    teko_b3_g(s, 2, 6, 10, 14, m[4], m[5]);
    teko_b3_g(s, 3, 7, 11, 15, m[6], m[7]);
    // Diagonals.
    teko_b3_g(s, 0, 5, 10, 15, m[8], m[9]);
    teko_b3_g(s, 1, 6, 11, 12, m[10], m[11]);
    teko_b3_g(s, 2, 7, 8, 13, m[12], m[13]);
    teko_b3_g(s, 3, 4, 9, 14, m[14], m[15]);
}

static void teko_b3_permute(uint32_t m[16]) {
    uint32_t tmp[16];
    unsigned i;
    for (i = 0u; i < 16u; ++i) {
        tmp[i] = m[TEKO_B3_MSG_PERM[i]];
    }
    memcpy(m, tmp, sizeof(tmp));
}

// The compression function; writes 16 output words.
static void teko_b3_compress(const uint32_t cv[8], const uint32_t block[16],
                             uint64_t counter, uint32_t block_len, uint32_t flags,
                             uint32_t out[16]) {
    uint32_t s[16];
    uint32_t m[16];
    unsigned i, r;

    s[0] = cv[0]; s[1] = cv[1]; s[2] = cv[2]; s[3] = cv[3];
    s[4] = cv[4]; s[5] = cv[5]; s[6] = cv[6]; s[7] = cv[7];
    s[8] = TEKO_B3_IV[0]; s[9] = TEKO_B3_IV[1]; s[10] = TEKO_B3_IV[2]; s[11] = TEKO_B3_IV[3];
    s[12] = (uint32_t)counter;
    s[13] = (uint32_t)(counter >> 32);
    s[14] = block_len;
    s[15] = flags;

    memcpy(m, block, sizeof(m));

    for (r = 0u; r < 7u; ++r) {
        teko_b3_round(s, m);
        if (r < 6u) teko_b3_permute(m);
    }

    for (i = 0u; i < 8u; ++i) {
        out[i] = s[i] ^ s[i + 8u];
        out[i + 8u] = s[i + 8u] ^ cv[i];
    }
}

static void teko_b3_words_from_le(const uint8_t* bytes, uint32_t* words, size_t nwords) {
    size_t i;
    for (i = 0u; i < nwords; ++i) {
        words[i] = ((uint32_t)bytes[i * 4u + 0u])
                 | ((uint32_t)bytes[i * 4u + 1u] << 8)
                 | ((uint32_t)bytes[i * 4u + 2u] << 16)
                 | ((uint32_t)bytes[i * 4u + 3u] << 24);
    }
}

// An "output": deferred compression that can yield either a chaining value or root bytes.
typedef struct {
    uint32_t input_chaining_value[8];
    uint32_t block_words[16];
    uint64_t counter;
    uint32_t block_len;
    uint32_t flags;
} TekoB3Output;

static void teko_b3_output_cv(const TekoB3Output* o, uint32_t cv[8]) {
    uint32_t out16[16];
    teko_b3_compress(o->input_chaining_value, o->block_words, o->counter, o->block_len, o->flags, out16);
    memcpy(cv, out16, 8u * sizeof(uint32_t));
}

static void teko_b3_output_root_bytes(const TekoB3Output* o, uint8_t* out, size_t out_len) {
    uint64_t counter = 0u;
    while (out_len > 0u) {
        uint32_t words[16];
        size_t block_out, i;
        teko_b3_compress(o->input_chaining_value, o->block_words, counter, o->block_len,
                         o->flags | TEKO_B3_ROOT, words);
        block_out = (out_len < 64u) ? out_len : 64u;
        for (i = 0u; i < block_out; ++i) {
            out[i] = (uint8_t)(words[i / 4u] >> (8u * (i % 4u)));
        }
        out += block_out;
        out_len -= block_out;
        counter++;
    }
}

static uint32_t teko_b3_chunk_start_flag(const TekoBlake3ChunkState* st) {
    return (st->blocks_compressed == 0u) ? TEKO_B3_CHUNK_START : 0u;
}

static size_t teko_b3_chunk_len(const TekoBlake3ChunkState* st) {
    return (size_t)TEKO_BLAKE3_BLOCK_LEN * (size_t)st->blocks_compressed + (size_t)st->block_len;
}

static void teko_b3_chunk_init(TekoBlake3ChunkState* st, const uint32_t key[8],
                               uint64_t counter, uint32_t flags) {
    memcpy(st->chaining_value, key, 8u * sizeof(uint32_t));
    st->chunk_counter = counter;
    memset(st->block, 0, TEKO_BLAKE3_BLOCK_LEN);
    st->block_len = 0u;
    st->blocks_compressed = 0u;
    st->flags = flags;
}

static void teko_b3_chunk_update(TekoBlake3ChunkState* st, const uint8_t* input, size_t len) {
    while (len > 0u) {
        if (st->block_len == TEKO_BLAKE3_BLOCK_LEN) {
            uint32_t block_words[16];
            uint32_t out16[16];
            teko_b3_words_from_le(st->block, block_words, 16u);
            teko_b3_compress(st->chaining_value, block_words, st->chunk_counter,
                             TEKO_BLAKE3_BLOCK_LEN, st->flags | teko_b3_chunk_start_flag(st), out16);
            memcpy(st->chaining_value, out16, 8u * sizeof(uint32_t));
            st->blocks_compressed++;
            memset(st->block, 0, TEKO_BLAKE3_BLOCK_LEN);
            st->block_len = 0u;
        }
        {
            size_t want = TEKO_BLAKE3_BLOCK_LEN - st->block_len;
            size_t take = (len < want) ? len : want;
            memcpy(st->block + st->block_len, input, take);
            st->block_len = (uint8_t)(st->block_len + take);
            input += take;
            len -= take;
        }
    }
}

static TekoB3Output teko_b3_chunk_output(const TekoBlake3ChunkState* st) {
    TekoB3Output o;
    memcpy(o.input_chaining_value, st->chaining_value, 8u * sizeof(uint32_t));
    teko_b3_words_from_le(st->block, o.block_words, 16u);
    o.counter = st->chunk_counter;
    o.block_len = st->block_len;
    o.flags = st->flags | teko_b3_chunk_start_flag(st) | TEKO_B3_CHUNK_END;
    return o;
}

static TekoB3Output teko_b3_parent_output(const uint32_t left[8], const uint32_t right[8],
                                          const uint32_t key[8], uint32_t flags) {
    TekoB3Output o;
    memcpy(o.input_chaining_value, key, 8u * sizeof(uint32_t));
    memcpy(o.block_words, left, 8u * sizeof(uint32_t));
    memcpy(o.block_words + 8, right, 8u * sizeof(uint32_t));
    o.counter = 0u;
    o.block_len = TEKO_BLAKE3_BLOCK_LEN;
    o.flags = TEKO_B3_PARENT | flags;
    return o;
}

static void teko_b3_parent_cv(const uint32_t left[8], const uint32_t right[8],
                              const uint32_t key[8], uint32_t flags, uint32_t cv[8]) {
    TekoB3Output o = teko_b3_parent_output(left, right, key, flags);
    teko_b3_output_cv(&o, cv);
}

void teko_blake3_init(TekoBlake3Ctx* ctx) {
    teko_b3_chunk_init(&ctx->chunk_state, TEKO_B3_IV, 0u, 0u);
    memcpy(ctx->key, TEKO_B3_IV, 8u * sizeof(uint32_t));
    ctx->cv_stack_len = 0u;
    ctx->flags = 0u;
}

static void teko_b3_push(TekoBlake3Ctx* ctx, const uint32_t cv[8]) {
    memcpy(ctx->cv_stack[ctx->cv_stack_len], cv, 8u * sizeof(uint32_t));
    ctx->cv_stack_len++;
}

// Merge the new chunk CV into the tree: pop+combine for each trailing zero bit of the
// total chunk count, then push the result.
static void teko_b3_add_chunk_cv(TekoBlake3Ctx* ctx, uint32_t new_cv[8], uint64_t total_chunks) {
    while ((total_chunks & 1u) == 0u) {
        uint32_t left[8];
        ctx->cv_stack_len--;
        memcpy(left, ctx->cv_stack[ctx->cv_stack_len], 8u * sizeof(uint32_t));
        teko_b3_parent_cv(left, new_cv, ctx->key, ctx->flags, new_cv);
        total_chunks >>= 1;
    }
    teko_b3_push(ctx, new_cv);
}

void teko_blake3_update(TekoBlake3Ctx* ctx, const uint8_t* data, size_t len) {
    while (len > 0u) {
        if (teko_b3_chunk_len(&ctx->chunk_state) == TEKO_BLAKE3_CHUNK_LEN) {
            TekoB3Output o = teko_b3_chunk_output(&ctx->chunk_state);
            uint32_t chunk_cv[8];
            uint64_t total_chunks;
            teko_b3_output_cv(&o, chunk_cv);
            total_chunks = ctx->chunk_state.chunk_counter + 1u;
            teko_b3_add_chunk_cv(ctx, chunk_cv, total_chunks);
            teko_b3_chunk_init(&ctx->chunk_state, ctx->key, total_chunks, ctx->flags);
        }
        {
            size_t want = TEKO_BLAKE3_CHUNK_LEN - teko_b3_chunk_len(&ctx->chunk_state);
            size_t take = (len < want) ? len : want;
            teko_b3_chunk_update(&ctx->chunk_state, data, take);
            data += take;
            len -= take;
        }
    }
}

void teko_blake3_finalize(TekoBlake3Ctx* ctx, uint8_t* out, size_t out_len) {
    TekoB3Output output = teko_b3_chunk_output(&ctx->chunk_state);
    int remaining = (int)ctx->cv_stack_len;
    while (remaining > 0) {
        uint32_t right_cv[8];
        remaining--;
        teko_b3_output_cv(&output, right_cv);
        output = teko_b3_parent_output(ctx->cv_stack[remaining], right_cv, ctx->key, ctx->flags);
    }
    teko_b3_output_root_bytes(&output, out, out_len);
}

void teko_blake3(const uint8_t* data, size_t len, uint8_t out[TEKO_BLAKE3_OUT_LEN]) {
    TekoBlake3Ctx ctx;
    teko_blake3_init(&ctx);
    teko_blake3_update(&ctx, data, len);
    teko_blake3_finalize(&ctx, out, TEKO_BLAKE3_OUT_LEN);
}
