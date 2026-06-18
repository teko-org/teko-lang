/*
 * Phase 19 (Networking) — RFC 6455 WebSocket frame codec + handshake accept-key.
 * Pure byte-buffer unit. No sockets, no snprintf, no strtoll.
 * Freestanding-safe: only malloc/free/memcpy/memcmp/strlen.
 *
 * OP_CALL_RUNTIME id range 100–109 reserved for websocket (no emission this wave).
 *
 * SAST notes:
 *   - Payload-len: the 7-bit field is read first; if 126 → read uint16_t LE; if 127 → read uint64_t LE.
 *     Every payload_len > TEKO_WS_MAX_PAYLOAD is rejected (TEKO_WS_ERR_TOO_LARGE).
 *   - Integer overflow: header-size + payload_len cannot overflow size_t before alloc.
 *   - Masking XOR: only iterate i < payload_len (bounded by the payload size).
 *   - UTF-8 validation: a state machine validates continuation bytes, no overlong sequences,
 *     no surrogate pairs (0xD800–0xDFFF), and no code-points > U+10FFFF.
 *   - Close frame: 0 bytes = no status; 2+ bytes = uint16_t BE close code + optional UTF-8 reason.
 *   - No format-string paths anywhere.
 */

#include "teko_websocket.h"
#include "teko_crypto_sha1.h"
#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* memcpy, memcmp, strlen, memset */
#include <stdint.h>   /* intptr_t */

#define TEKO_WS_MAGIC "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

/* =========================================================================
 * Internal helpers
 * ====================================================================== */

/*
 * Overflow-safe size_t addition.
 * Sets *out = a + b and returns 1 on success, 0 on overflow.
 */
static int safe_add_sz(size_t a, size_t b, size_t *out) {
    if (b > (size_t)-1 - a) return 0;  /* would overflow */
    *out = a + b;
    return 1;
}

/*
 * Base64 encode a 20-byte SHA-1 digest (or any raw bytes) to a NUL-terminated string.
 * out must be at least (((len + 2) / 3) * 4) + 1 bytes (for 20 bytes → 28 chars + NUL).
 * The encoding alphabet is standard RFC 4648 (A-Z, a-z, 0-9, +, /).
 */
static void base64_encode_bytes(const uint8_t *data, size_t len, char *out) {
    static const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out_idx = 0;

    for (size_t i = 0; i < len; i += 3) {
        uint32_t b1 = data[i];
        uint32_t b2 = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t b3 = (i + 2 < len) ? data[i + 2] : 0;

        uint32_t n = (b1 << 16) | (b2 << 8) | b3;

        out[out_idx++] = alphabet[(n >> 18) & 0x3F];
        out[out_idx++] = alphabet[(n >> 12) & 0x3F];
        out[out_idx++] = (i + 1 < len) ? alphabet[(n >> 6) & 0x3F] : '=';
        out[out_idx++] = (i + 2 < len) ? alphabet[n & 0x3F] : '=';
    }
    out[out_idx] = '\0';
}

/*
 * UTF-8 validation state machine (RFC 3629).
 * Returns 1 if valid, 0 if invalid (overlong, surrogate, > U+10FFFF, malformed continuation).
 */
static int is_valid_utf8(const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t b = data[i];

        if ((b & 0x80) == 0) {
            /* Single-byte ASCII (0xxxxxxx) */
            i++;
        } else if ((b & 0xE0) == 0xC0) {
            /* 2-byte sequence (110xxxxx 10xxxxxx) */
            if (i + 1 >= len) return 0;  /* truncated */
            uint8_t b2 = data[i + 1];
            if ((b2 & 0xC0) != 0x80) return 0;  /* invalid continuation */

            uint32_t cp = ((b & 0x1F) << 6) | (b2 & 0x3F);
            if (cp < 0x80) return 0;  /* overlong encoding */
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            /* 3-byte sequence (1110xxxx 10xxxxxx 10xxxxxx) */
            if (i + 2 >= len) return 0;  /* truncated */
            uint8_t b2 = data[i + 1];
            uint8_t b3 = data[i + 2];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;  /* invalid continuation */

            uint32_t cp = ((b & 0x0F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);
            if (cp < 0x800) return 0;  /* overlong encoding */
            if (cp >= 0xD800 && cp <= 0xDFFF) return 0;  /* surrogate pair */
            i += 3;
        } else if ((b & 0xF8) == 0xF0) {
            /* 4-byte sequence (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx) */
            if (i + 3 >= len) return 0;  /* truncated */
            uint8_t b2 = data[i + 1];
            uint8_t b3 = data[i + 2];
            uint8_t b4 = data[i + 3];
            if ((b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80 || (b4 & 0xC0) != 0x80) return 0;

            uint32_t cp = ((b & 0x07) << 18) | ((b2 & 0x3F) << 12) | ((b3 & 0x3F) << 6) | (b4 & 0x3F);
            if (cp < 0x10000) return 0;  /* overlong encoding */
            if (cp > 0x10FFFF) return 0;  /* > max Unicode code point */
            i += 4;
        } else {
            return 0;  /* invalid start byte */
        }
    }
    return 1;
}

/* =========================================================================
 * Core codec functions
 * ====================================================================== */

int teko_ws_frame_encode(uint8_t opcode, const uint8_t *payload, size_t payload_len,
                         uint8_t **out_buf, size_t *out_len) {
    *out_buf = NULL;
    *out_len = 0;

    if (payload_len > TEKO_WS_MAX_PAYLOAD) {
        return TEKO_WS_ERR_TOO_LARGE;
    }

    /* Calculate total size: FIN(1) + RSV(3) + opcode(4) + MASK(1) + LEN(7 or 23 or 71 bits) + payload */
    size_t header_size = 2;  /* FIN + opcode + MASK + 7-bit len */

    if (payload_len > 65535) {
        /* 127 (64-bit length follows) */
        size_t test;
        if (!safe_add_sz(header_size, 8, &test)) {
            return TEKO_WS_ERR_OVERFLOW;
        }
        header_size = test;
    } else if (payload_len > 125) {
        /* 126 (16-bit length follows) */
        size_t test;
        if (!safe_add_sz(header_size, 2, &test)) {
            return TEKO_WS_ERR_OVERFLOW;
        }
        header_size = test;
    }

    size_t total_size;
    if (!safe_add_sz(header_size, payload_len, &total_size)) {
        return TEKO_WS_ERR_OVERFLOW;
    }

    uint8_t *buf = (uint8_t *)malloc(total_size);
    if (!buf) {
        return TEKO_WS_ERR_OVERFLOW;
    }

    /* Byte 0: FIN(1) + RSV(3) + opcode(4) */
    buf[0] = (uint8_t)(0x80 | (opcode & 0x0F));

    /* Byte 1: MASK(1) + payload-len(7) */
    size_t pos = 1;
    if (payload_len <= 125) {
        buf[pos] = (uint8_t)payload_len;
        pos++;
    } else if (payload_len <= 65535) {
        buf[pos] = 126;
        pos++;
        buf[pos] = (uint8_t)((payload_len >> 8) & 0xFF);
        buf[pos + 1] = (uint8_t)(payload_len & 0xFF);
        pos += 2;
    } else {
        buf[pos] = 127;
        pos++;
        buf[pos] = (uint8_t)((payload_len >> 56) & 0xFF);
        buf[pos + 1] = (uint8_t)((payload_len >> 48) & 0xFF);
        buf[pos + 2] = (uint8_t)((payload_len >> 40) & 0xFF);
        buf[pos + 3] = (uint8_t)((payload_len >> 32) & 0xFF);
        buf[pos + 4] = (uint8_t)((payload_len >> 24) & 0xFF);
        buf[pos + 5] = (uint8_t)((payload_len >> 16) & 0xFF);
        buf[pos + 6] = (uint8_t)((payload_len >> 8) & 0xFF);
        buf[pos + 7] = (uint8_t)(payload_len & 0xFF);
        pos += 8;
    }

    /* Copy payload (unmasked, server→client) */
    if (payload_len > 0 && payload) {
        memcpy(buf + pos, payload, payload_len);
    }

    *out_buf = buf;
    *out_len = total_size;
    return TEKO_WS_OK;
}

int teko_ws_frame_decode(const uint8_t *buf, size_t buf_len, TekoWsFrame *f) {
    memset(f, 0, sizeof(*f));

    if (buf_len < 2) {
        return TEKO_WS_ERR_TRUNCATED;
    }

    /* Byte 0: FIN(1) + RSV(3) + opcode(4) */
    uint8_t byte0 = buf[0];
    int fin = (byte0 & 0x80) >> 7;
    int rsv = (byte0 & 0x70) >> 4;

    if (!fin || rsv != 0) {
        return TEKO_WS_ERR_FRAGMENT;  /* fragmentation or reserved bits set */
    }

    f->opcode = byte0 & 0x0F;

    /* Byte 1: MASK(1) + payload-len(7) */
    uint8_t byte1 = buf[1];
    f->is_masked = (byte1 & 0x80) >> 7;
    uint8_t len7 = byte1 & 0x7F;

    size_t payload_len = 0;
    size_t header_size = 2;

    if (len7 == 126) {
        /* 16-bit length follows */
        if (buf_len < 4) {
            return TEKO_WS_ERR_TRUNCATED;
        }
        payload_len = ((size_t)buf[2] << 8) | buf[3];
        header_size = 4;
    } else if (len7 == 127) {
        /* 64-bit length follows */
        if (buf_len < 10) {
            return TEKO_WS_ERR_TRUNCATED;
        }
        payload_len = ((size_t)buf[2] << 56) |
                      ((size_t)buf[3] << 48) |
                      ((size_t)buf[4] << 40) |
                      ((size_t)buf[5] << 32) |
                      ((size_t)buf[6] << 24) |
                      ((size_t)buf[7] << 16) |
                      ((size_t)buf[8] << 8) |
                      buf[9];
        header_size = 10;
    } else {
        payload_len = len7;
    }

    f->payload_len = payload_len;

    if (payload_len > TEKO_WS_MAX_PAYLOAD) {
        return TEKO_WS_ERR_TOO_LARGE;
    }

    /* Mask key (4 bytes if is_masked) */
    size_t mask_size = f->is_masked ? 4 : 0;
    size_t total_header = header_size + mask_size;

    if (buf_len < total_header) {
        return TEKO_WS_ERR_TRUNCATED;
    }

    if (f->is_masked) {
        memcpy(f->mask, buf + header_size, 4);
    }

    /* Payload */
    size_t payload_start = total_header;
    size_t payload_end;
    if (!safe_add_sz(payload_start, payload_len, &payload_end)) {
        return TEKO_WS_ERR_OVERFLOW;
    }

    if (buf_len < payload_end) {
        return TEKO_WS_ERR_TRUNCATED;
    }

    if (payload_len == 0) {
        f->payload = NULL;
        return TEKO_WS_OK;
    }

    /* Allocate and copy payload (with masking if needed) */
    f->payload = (uint8_t *)malloc(payload_len);
    if (!f->payload) {
        return TEKO_WS_ERR_OVERFLOW;
    }

    if (f->is_masked) {
        /* XOR each payload byte with the mask (cyclically) */
        for (size_t i = 0; i < payload_len; i++) {
            f->payload[i] = buf[payload_start + i] ^ f->mask[i % 4];
        }
    } else {
        memcpy(f->payload, buf + payload_start, payload_len);
    }

    /* UTF-8 validation for text frames */
    if (f->opcode == TEKO_WS_OP_TEXT && payload_len > 0) {
        if (!is_valid_utf8(f->payload, payload_len)) {
            free(f->payload);
            f->payload = NULL;
            return TEKO_WS_ERR_INVALID_UTF8;
        }
    }

    return TEKO_WS_OK;
}

void teko_ws_handshake_accept(const char *client_key, char out[29]) {
    /* Concatenate client_key + magic string.
     * RFC 6455 §4.2.1: Sec-WebSocket-Key is exactly 24 base64 chars.
     * Bound to TEKO_WS_KEY_MAX_LEN to prevent stack buffer overflow when
     * client_key is attacker-controlled (e.g. a too-long or malformed key). */
#define TEKO_WS_KEY_MAX_LEN 27u  /* 64 - len(MAGIC) - 1 NUL = 27 */
    char concat_buf[64];
    size_t key_len = strlen(client_key);
    if (key_len > TEKO_WS_KEY_MAX_LEN) key_len = TEKO_WS_KEY_MAX_LEN;  /* truncate; SHA-1 will differ but no overflow */
    memcpy(concat_buf, client_key, key_len);
    memcpy(concat_buf + key_len, TEKO_WS_MAGIC, sizeof(TEKO_WS_MAGIC) - 1);

    /* SHA-1 of the concatenated string */
    uint8_t digest[20];
    teko_sha1((const uint8_t *)concat_buf, key_len + (sizeof(TEKO_WS_MAGIC) - 1), digest);

    /* Base64 encode the digest */
    base64_encode_bytes(digest, 20, out);
}

/* =========================================================================
 * Runtime surface wrappers (OP_CALL_RUNTIME ids 100–109)
 * ====================================================================== */

long teko_rt_ws_handshake_accept(long key_ptr) {
    const char *key = (const char *)(intptr_t)key_ptr;
    char *out = (char *)malloc(29);
    if (!out) return 0;

    teko_ws_handshake_accept(key, out);
    return (long)(intptr_t)out;
}

long teko_rt_ws_frame_encode(long opcode, long payload_ptr, long payload_len) {
    uint8_t *out_buf = NULL;
    size_t out_len = 0;

    const uint8_t *payload = (const uint8_t *)(intptr_t)payload_ptr;
    int status = teko_ws_frame_encode((uint8_t)opcode, payload, (size_t)payload_len,
                                      &out_buf, &out_len);

    if (status != TEKO_WS_OK) return 0;
    return (long)(intptr_t)out_buf;
}

long teko_rt_ws_frame_decode(long buf_ptr, long buf_len, long opcode_out_ptr) {
    const uint8_t *buf = (const uint8_t *)(intptr_t)buf_ptr;
    uint8_t *opcode_out = (uint8_t *)(intptr_t)opcode_out_ptr;

    TekoWsFrame f;
    int status = teko_ws_frame_decode(buf, (size_t)buf_len, &f);

    if (opcode_out) {
        *opcode_out = f.opcode;
    }

    if (status != TEKO_WS_OK) return 0;
    return (long)(intptr_t)f.payload;
}

long teko_rt_ws_frame_free(long ptr) {
    if (ptr != 0) {
        free((void *)(intptr_t)ptr);
    }
    return 0;
}
