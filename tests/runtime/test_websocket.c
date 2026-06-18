/*
 * Phase 19 (Networking) — RFC 6455 WebSocket frame codec + handshake KATs.
 * Unity test suite for teko_websocket.c.
 */

#include "unity.h"
#include "../../src/runtime/teko_websocket.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Handshake KATs
 * ====================================================================== */

void test_teko_ws_handshake_accept_rfc6455(void) {
    /* RFC 6455 §1.3 example:
       Input:  "dGhlIHNhbXBsZSBub25jZQ=="
       Expected output: "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="
    */
    char out[29];
    teko_ws_handshake_accept("dGhlIHNhbXBsZSBub25jZQ==", out);
    TEST_ASSERT_EQUAL_STRING("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", out);
}

/* =========================================================================
 * Frame encoding KATs
 * ====================================================================== */

void test_teko_ws_frame_encode_text_hello(void) {
    /* RFC 6455 §5.6: unmasked text frame with "Hello"
       Expected wire: 0x81 0x05 0x48 0x65 0x6c 0x6c 0x6f
       (FIN=1, opcode=0x1, no mask, len=5, payload="Hello")
    */
    const uint8_t payload[] = "Hello";
    uint8_t *out = NULL;
    size_t out_len = 0;

    int status = teko_ws_frame_encode(TEKO_WS_OP_TEXT, payload, 5, &out, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_OK, status);
    TEST_ASSERT_EQUAL_UINT32(7, (uint32_t)out_len);

    uint8_t expected[] = {0x81, 0x05, 0x48, 0x65, 0x6c, 0x6c, 0x6f};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 7);

    free(out);
}

void test_teko_ws_frame_encode_close_empty(void) {
    /* Empty close frame (no status code) */
    uint8_t *out = NULL;
    size_t out_len = 0;

    int status = teko_ws_frame_encode(TEKO_WS_OP_CLOSE, NULL, 0, &out, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_OK, status);
    TEST_ASSERT_EQUAL_UINT32(2, (uint32_t)out_len);

    uint8_t expected[] = {0x88, 0x00};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, out, 2);

    free(out);
}

void test_teko_ws_frame_encode_payload_too_large(void) {
    /* Payload exceeds TEKO_WS_MAX_PAYLOAD */
    uint8_t *out = NULL;
    size_t out_len = 0;
    size_t huge = TEKO_WS_MAX_PAYLOAD + 1;

    int status = teko_ws_frame_encode(TEKO_WS_OP_TEXT, NULL, huge, &out, &out_len);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_ERR_TOO_LARGE, status);
    TEST_ASSERT_NULL(out);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)out_len);
}

/* =========================================================================
 * Frame decoding KATs
 * ====================================================================== */

void test_teko_ws_frame_decode_masked_hello(void) {
    /* RFC 6455 §5.6: masked frame (client→server)
       Input: 0x81 0x85 0x37 0xfa 0x21 0x3d 0x7f 0x9f 0x4d 0x51 0x58
       Opcode: 0x1 (text)
       Masked: yes (0x85 & 0x80 = 0x80)
       Mask: 0x37 0xfa 0x21 0x3d
       Masked payload: 0x7f 0x9f 0x4d 0x51 0x58
       Unmasked payload (XOR with mask): "Hello"
    */
    uint8_t wire[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);

    TEST_ASSERT_EQUAL_INT(TEKO_WS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(TEKO_WS_OP_TEXT, f.opcode);
    TEST_ASSERT_EQUAL_INT(1, f.is_masked);
    TEST_ASSERT_EQUAL_UINT32(5, (uint32_t)f.payload_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello", f.payload, 5);

    free(f.payload);
}

void test_teko_ws_frame_decode_text_utf8_valid(void) {
    /* Unmasked text frame with valid UTF-8: "Hello, 世界" */
    uint8_t hello[] = "Hello, ";
    uint8_t world[] = {0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C};  /* "世界" in UTF-8 */
    size_t hello_len = 7;
    size_t world_len = 6;
    size_t payload_len = hello_len + world_len;

    /* Build frame: FIN=1, opcode=1, no mask, len=13 */
    uint8_t wire[2 + 13];
    wire[0] = 0x81;
    wire[1] = (uint8_t)payload_len;
    memcpy(wire + 2, hello, hello_len);
    memcpy(wire + 2 + hello_len, world, world_len);

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);

    TEST_ASSERT_EQUAL_INT(TEKO_WS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(TEKO_WS_OP_TEXT, f.opcode);
    TEST_ASSERT_EQUAL_INT(0, f.is_masked);
    TEST_ASSERT_EQUAL_UINT32(payload_len, (uint32_t)f.payload_len);

    free(f.payload);
}

void test_teko_ws_frame_decode_payload_too_large(void) {
    /* Craft a 127-type extended length with a value > TEKO_WS_MAX_PAYLOAD */
    uint8_t wire[] = {
        0x81,  /* FIN=1, opcode=1 (text) */
        0x7F,  /* no mask, 64-bit length follows */
        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00  /* 2^32 bytes (huge) */
    };

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_ERR_TOO_LARGE, status);
    TEST_ASSERT_NULL(f.payload);
}

void test_teko_ws_frame_decode_truncated_header(void) {
    /* Buffer too short to contain a complete frame header */
    uint8_t wire[] = {0x81};  /* only 1 byte */

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, 1, &f);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_ERR_TRUNCATED, status);
    TEST_ASSERT_NULL(f.payload);
}

void test_teko_ws_frame_decode_truncated_payload(void) {
    /* Header says payload is 5 bytes, but only 3 present */
    uint8_t wire[] = {
        0x81,  /* FIN=1, opcode=1 */
        0x05,  /* no mask, len=5 */
        0x48, 0x65, 0x6c  /* only "Hel" (3 bytes) */
    };

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_ERR_TRUNCATED, status);
    TEST_ASSERT_NULL(f.payload);
}

void test_teko_ws_frame_decode_invalid_utf8(void) {
    /* Text frame with invalid UTF-8 (lone continuation byte) */
    uint8_t wire[] = {
        0x81,  /* FIN=1, opcode=1 (text) */
        0x01,  /* no mask, len=1 */
        0x80   /* lone continuation byte (invalid) */
    };

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_ERR_INVALID_UTF8, status);
    TEST_ASSERT_NULL(f.payload);
}

void test_teko_ws_frame_decode_binary_no_utf8_validation(void) {
    /* Binary frame with the same invalid UTF-8 should NOT be rejected */
    uint8_t wire[] = {
        0x82,  /* FIN=1, opcode=2 (binary) */
        0x01,  /* no mask, len=1 */
        0x80   /* lone continuation byte — OK for binary */
    };

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_OK, status);
    TEST_ASSERT_EQUAL_UINT8(TEKO_WS_OP_BINARY, f.opcode);
    TEST_ASSERT_EQUAL_UINT32(1, (uint32_t)f.payload_len);
    TEST_ASSERT_EQUAL_UINT8(0x80, f.payload[0]);

    free(f.payload);
}

void test_teko_ws_frame_decode_fragmented_rejected(void) {
    /* FIN=0 means fragmented; we reject it */
    uint8_t wire[] = {
        0x01,  /* FIN=0, opcode=1 (fragmented text) */
        0x05,  /* no mask, len=5 */
        0x48, 0x65, 0x6c, 0x6c, 0x6f
    };

    TekoWsFrame f;
    int status = teko_ws_frame_decode(wire, sizeof(wire), &f);
    TEST_ASSERT_EQUAL_INT(TEKO_WS_ERR_FRAGMENT, status);
    TEST_ASSERT_NULL(f.payload);
}
