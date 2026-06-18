#ifndef TEKO_WEBSOCKET_H
#define TEKO_WEBSOCKET_H

/*
 * Phase 19 (Networking) — RFC 6455 WebSocket frame codec + handshake accept-key.
 * Pure byte-buffer unit: encode/decode WebSocket frames; compute handshake accept key.
 * No sockets, no frontend wiring this wave (integration = WS-SRV, Wave 2).
 *
 * OP_CALL_RUNTIME id range 100–109 reserved for websocket (no emission/frontend wiring this wave).
 *
 * Design contract:
 *   - Freestanding-safe: only malloc/free/memcpy/strlen/memcmp.  No snprintf/strtoll/sscanf.
 *   - MSVC-portable: no C23 auto/nullptr; no __attribute__; TEKO_PACKED not used here.
 *   - Attacker-controlled input: WS frame bytes are untrusted. Every length is bounds-checked
 *     before any copy. Integer arithmetic uses overflow-safe patterns (see TEKO_WS_MAX_PAYLOAD).
 *   - Ownership: encode/decode return heap buffers the caller frees with free().
 *   - Error handling: all functions return TEKO_WS_OK (0) on success, a negative error
 *     code on failure.  Partial output is never produced.
 *   - No format-string paths: frame data is treated as opaque byte sequences.
 *   - UTF-8 validation: text frames validate payload via a state machine (no overlong, no surrogates).
 */

#include <stddef.h>  /* size_t */
#include <stdint.h>  /* uint8_t, uint16_t, uint64_t */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Limits (compile-time caps; checked before every copy / allocation)
 * ---------------------------------------------------------------------- */

/* Maximum payload we will allocate for a single decoded frame (64 MiB).
   Guard against integer overflow: any payload_len that exceeds this is rejected. */
#define TEKO_WS_MAX_PAYLOAD  ((size_t)64 * 1024 * 1024)

/* -------------------------------------------------------------------------
 * WebSocket opcodes (RFC 6455 §5.2)
 * ---------------------------------------------------------------------- */

#define TEKO_WS_OP_CONTINUATION  0x0
#define TEKO_WS_OP_TEXT          0x1
#define TEKO_WS_OP_BINARY        0x2
#define TEKO_WS_OP_CLOSE         0x8
#define TEKO_WS_OP_PING          0x9
#define TEKO_WS_OP_PONG          0xA

/* -------------------------------------------------------------------------
 * Return codes
 * ---------------------------------------------------------------------- */

#define TEKO_WS_OK                (-0)
#define TEKO_WS_ERR_TOO_LARGE     (-1)  /* payload_len > TEKO_WS_MAX_PAYLOAD */
#define TEKO_WS_ERR_OVERFLOW      (-2)  /* integer overflow in length computation */
#define TEKO_WS_ERR_INVALID_UTF8  (-3)  /* text frame with invalid UTF-8 payload */
#define TEKO_WS_ERR_FRAGMENT      (-4)  /* RSV bits set (reserved/fragmentation) */
#define TEKO_WS_ERR_TRUNCATED     (-5)  /* incomplete frame (buffer too short) */

/* -------------------------------------------------------------------------
 * Decoded frame structure
 * ---------------------------------------------------------------------- */

typedef struct {
    uint8_t  opcode;      /* TEKO_WS_OP_* */
    int      is_masked;   /* 1 = client-to-server (mask key present); 0 = unmasked (server→client) */
    uint8_t  mask[4];     /* mask key (all zeros when is_masked==0) */
    size_t   payload_len;
    uint8_t *payload;     /* heap-allocated; caller must free() with free() or teko_rt_ws_frame_free() */
} TekoWsFrame;

/* -------------------------------------------------------------------------
 * Core codec functions
 * ---------------------------------------------------------------------- */

/* Encode an UNMASKED server-to-client frame.
   - opcode: one of TEKO_WS_OP_* (TEXT, BINARY, CLOSE, PING, PONG, etc.)
   - payload: the frame body (may be NULL if payload_len==0)
   - payload_len: byte count (must fit within TEKO_WS_MAX_PAYLOAD)
   - out_buf: output pointer (caller must free with free() or teko_rt_ws_frame_free())
   - out_len: output byte count (includes header + payload)
   Returns TEKO_WS_OK (0) on success, negative error code on failure.
   On error, *out_buf is set to NULL and *out_len to 0. */
int teko_ws_frame_encode(uint8_t opcode, const uint8_t *payload, size_t payload_len,
                         uint8_t **out_buf, size_t *out_len);

/* Decode one complete WebSocket frame from the buffer.
   - buf: pointer to the start of the frame header (must be fully present in the buffer)
   - buf_len: byte count of buf (must include the complete frame)
   - f: output frame struct (caller must free f->payload with free() or teko_rt_ws_frame_free())
   Returns TEKO_WS_OK (0) on success, negative error code on failure.
   On error, f->payload is set to NULL and other fields are left at their initial values.
   SAST: payload_len is validated against TEKO_WS_MAX_PAYLOAD before alloc; masking loop
         is bounded by payload_len; frame buffer bounds are checked before every read. */
int teko_ws_frame_decode(const uint8_t *buf, size_t buf_len, TekoWsFrame *f);

/* Compute the RFC 6455 Sec-WebSocket-Accept header value.
   - client_key: the value of the incoming Sec-WebSocket-Key header (base64, 24 chars + NUL)
   - out: output buffer (must be at least 29 bytes: 28 chars + NUL)
   Writes a NUL-terminated 28-char base64 string.
   Uses teko_sha1() (imported from teko_crypto_sha1.h) and an inline 20-byte→base64 helper. */
void teko_ws_handshake_accept(const char *client_key, char out[29]);

/* -------------------------------------------------------------------------
 * Runtime surface wrappers (OP_CALL_RUNTIME ids 100–109)
 * For native: linked into teko_rt archive; called by emitted code as (long) → long ABI.
 * For WASM: reactor imports from the "crypto" namespace (same pattern as Phase 14+ crypto).
 * All args/returns are register-width (long / intptr_t).
 * ---------------------------------------------------------------------- */

/* teko_rt_ws_handshake_accept(key_ptr: char*) → char*
   Allocates and returns a NUL-terminated 29-byte accept string.
   Caller must free with teko_rt_ws_frame_free(). */
long teko_rt_ws_handshake_accept(long key_ptr);

/* teko_rt_ws_frame_encode(opcode, payload_ptr, payload_len) → char*
   Allocates and returns wire bytes (header + masked/unmasked payload).
   Return pointer is stored in a heap buffer; caller must free with teko_rt_ws_frame_free(). */
long teko_rt_ws_frame_encode(long opcode, long payload_ptr, long payload_len);

/* teko_rt_ws_frame_decode(buf_ptr, buf_len, opcode_out_ptr) → char*
   Decodes one frame and returns the payload (or NULL on error).
   The opcode is written to *(uint8_t*)opcode_out_ptr.
   Caller must free the returned payload with teko_rt_ws_frame_free(). */
long teko_rt_ws_frame_decode(long buf_ptr, long buf_len, long opcode_out_ptr);

/* teko_rt_ws_frame_free(ptr) → 0
   Frees a heap-allocated frame or string (payload or encoded wire bytes).
   Safe to call with NULL. Always returns 0. */
long teko_rt_ws_frame_free(long ptr);

#ifdef __cplusplus
}
#endif

#endif /* TEKO_WEBSOCKET_H */
