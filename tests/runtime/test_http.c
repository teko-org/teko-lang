/*
 * Phase 19 (Networking) — KATs for the HTTP/1.1 codec (teko_http.c).
 * Pure unit tests: fixed byte vectors, no sockets, no networking.
 *
 * Coverage:
 *   - build_request: basic GET, POST with body and headers, auto Content-Length
 *   - build_response: 200 OK with body, 404 no body, auto Content-Length
 *   - parse_request: GET, POST, header round-trip, chunked body
 *   - parse_response: 200 with body, chunked body
 *   - chunked_encode / chunked_decode round-trip
 *   - chunked_decode error paths: missing CRLF, oversized hex, bad hex digit, integer overflow
 *   - parse_request error paths: missing CRLF, obs-fold, duplicate Content-Length, body too large
 *   - find_header: case-insensitive lookup
 */

#include "unity.h"
#include "../../src/runtime/teko_http.h"
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Helpers
 * ====================================================================== */

/* =========================================================================
 * build_request
 * ====================================================================== */

void test_http_build_request_get(void) {
    char   *buf = NULL;
    size_t  len = 0;
    TekoHttpHeader hdrs[1];
    hdrs[0].name  = "Host";
    hdrs[0].value = "example.com";
    int rc = teko_http_build_request("GET", "/index.html", hdrs, 1, NULL, 0, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_GREATER_THAN(0, (int)len);
    /* Request-line present. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "GET /index.html HTTP/1.1\r\n"));
    /* Host header present. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Host: example.com\r\n"));
    /* No spurious Content-Length for GET with no body. */
    TEST_ASSERT_NULL(strstr(buf, "Content-Length"));
    /* Ends with blank line. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\r\n\r\n"));
    free(buf);
}

void test_http_build_request_post_with_body(void) {
    char   *buf = NULL;
    size_t  len = 0;
    const char *body = "{\"key\":\"value\"}";
    size_t body_len = strlen(body);
    TekoHttpHeader hdrs[2];
    hdrs[0].name  = "Host";
    hdrs[0].value = "api.example.com";
    hdrs[1].name  = "Content-Type";
    hdrs[1].value = "application/json";
    int rc = teko_http_build_request("POST", "/api/data", hdrs, 2, body, body_len, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "POST /api/data HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Type: application/json\r\n"));
    /* Auto Content-Length was injected. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Length: 15\r\n"));
    /* Body present at end. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "{\"key\":\"value\"}"));
    free(buf);
}

void test_http_build_request_no_auto_cl_when_cl_provided(void) {
    /* If caller already includes Content-Length, we must NOT add a second one. */
    char   *buf = NULL;
    size_t  len = 0;
    const char *body = "hello";
    TekoHttpHeader hdrs[1];
    hdrs[0].name  = "Content-Length";
    hdrs[0].value = "5";
    int rc = teko_http_build_request("PUT", "/resource", hdrs, 1, body, 5, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(buf);
    /* Exactly one Content-Length. */
    const char *first = strstr(buf, "Content-Length");
    TEST_ASSERT_NOT_NULL(first);
    const char *second = strstr(first + 1, "Content-Length");
    TEST_ASSERT_NULL(second);
    free(buf);
}

void test_http_build_request_null_args(void) {
    char  *buf = NULL;
    size_t len = 0;
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_build_request(NULL, "/", NULL, 0, NULL, 0, &buf, &len));
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_build_request("GET", NULL, NULL, 0, NULL, 0, &buf, &len));
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_build_request("GET", "/", NULL, 0, NULL, 0, NULL, &len));
}

void test_http_build_request_method_too_long(void) {
    char  *buf = NULL;
    size_t len = 0;
    /* 17 chars — exceeds TEKO_HTTP_MAX_METHOD (16). */
    int rc = teko_http_build_request("AVERYLONGMETHODXY", "/", NULL, 0, NULL, 0, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_METHOD_TOO_LONG, rc);
}

/* =========================================================================
 * build_response
 * ====================================================================== */

void test_http_build_response_200_with_body(void) {
    char   *buf = NULL;
    size_t  len = 0;
    const char *body = "<html>hello</html>";
    size_t body_len = strlen(body);
    TekoHttpHeader hdrs[1];
    hdrs[0].name  = "Content-Type";
    hdrs[0].value = "text/html";
    int rc = teko_http_build_response(200, "OK", hdrs, 1, body, body_len, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "HTTP/1.1 200 OK\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Type: text/html\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "Content-Length: 18\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "<html>hello</html>"));
    free(buf);
}

void test_http_build_response_404_no_body(void) {
    char   *buf = NULL;
    size_t  len = 0;
    int rc = teko_http_build_response(404, "Not Found", NULL, 0, NULL, 0, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "HTTP/1.1 404 Not Found\r\n"));
    TEST_ASSERT_NULL(strstr(buf, "Content-Length"));
    /* Ends with blank line. */
    TEST_ASSERT_NOT_NULL(strstr(buf, "\r\n\r\n"));
    free(buf);
}

void test_http_build_response_null_reason(void) {
    /* NULL reason_phrase is allowed (produces empty reason). */
    char   *buf = NULL;
    size_t  len = 0;
    int rc = teko_http_build_response(200, NULL, NULL, 0, NULL, 0, &buf, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(buf);
    TEST_ASSERT_NOT_NULL(strstr(buf, "HTTP/1.1 200 \r\n"));
    free(buf);
}

/* =========================================================================
 * parse_request
 * ====================================================================== */

static char *make_buf(const char *s, size_t *len) {
    *len = strlen(s);
    /* +1 so we can NUL-terminate inside the buffer safely. */
    char *b = (char *)malloc(*len + 2);
    if (!b) return NULL;
    memcpy(b, s, *len);
    b[*len]     = '\0';
    b[*len + 1] = '\0';
    return b;
}

void test_http_parse_request_get(void) {
    size_t len;
    char *buf = make_buf(
        "GET /hello HTTP/1.1\r\n"
        "Host: example.com\r\n"
        "Accept: text/plain\r\n"
        "\r\n",
        &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL_STRING("GET", req.method);
    TEST_ASSERT_EQUAL_STRING("/hello", req.path);
    TEST_ASSERT_EQUAL_STRING("HTTP/1.1", req.version);
    TEST_ASSERT_EQUAL(2, (int)req.header_count);
    TEST_ASSERT_EQUAL_STRING("Host", req.headers[0].name);
    TEST_ASSERT_EQUAL_STRING("example.com", req.headers[0].value);
    TEST_ASSERT_EQUAL_STRING("Accept", req.headers[1].name);
    TEST_ASSERT_EQUAL_STRING("text/plain", req.headers[1].value);
    TEST_ASSERT_NULL(req.body);
    TEST_ASSERT_EQUAL(0, (int)req.body_len);
    free(buf);
}

void test_http_parse_request_post_with_body(void) {
    const char *raw =
        "POST /data HTTP/1.1\r\n"
        "Host: api.example.com\r\n"
        "Content-Length: 7\r\n"
        "\r\n"
        "payload";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL_STRING("POST", req.method);
    TEST_ASSERT_EQUAL_STRING("/data", req.path);
    TEST_ASSERT_EQUAL(2, (int)req.header_count);
    TEST_ASSERT_NOT_NULL(req.body);
    TEST_ASSERT_EQUAL(7, (int)req.body_len);
    TEST_ASSERT_EQUAL_MEMORY("payload", req.body, 7);
    free(buf);
}

void test_http_parse_request_missing_crlf(void) {
    /* No terminal blank line. */
    size_t len;
    char *buf = make_buf("GET / HTTP/1.1\r\nHost: x.com\r\n", &len);
    TEST_ASSERT_NOT_NULL(buf);
    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_MALFORMED, rc);
    free(buf);
}

void test_http_parse_request_obs_fold_rejected(void) {
    /* obs-fold: a continuation line starting with SP. */
    const char *raw =
        "GET / HTTP/1.1\r\n"
        "Host: example.com\r\n"
        " continuation\r\n"
        "\r\n";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);
    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_OBS_FOLD, rc);
    free(buf);
}

void test_http_parse_request_duplicate_content_length(void) {
    /* Two Content-Length headers = request smuggling attempt. */
    const char *raw =
        "POST / HTTP/1.1\r\n"
        "Content-Length: 5\r\n"
        "Content-Length: 100\r\n"
        "\r\n"
        "hello";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);
    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_SMUGGLING, rc);
    free(buf);
}

void test_http_parse_request_null_args(void) {
    char dummy[4] = {0};
    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_parse_request(NULL, 0, &req));
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_parse_request(dummy, 0, NULL));
}

/* =========================================================================
 * parse_response
 * ====================================================================== */

void test_http_parse_response_200_with_body(void) {
    const char *raw =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: 5\r\n"
        "\r\n"
        "hello";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    int rc = teko_http_parse_response(buf, len, &resp);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL_STRING("HTTP/1.1", resp.version);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    TEST_ASSERT_EQUAL_STRING("OK", resp.reason_phrase);
    TEST_ASSERT_EQUAL(2, (int)resp.header_count);
    TEST_ASSERT_NOT_NULL(resp.body);
    TEST_ASSERT_EQUAL(5, (int)resp.body_len);
    TEST_ASSERT_EQUAL_MEMORY("hello", resp.body, 5);
    free(buf);
}

void test_http_parse_response_no_body(void) {
    const char *raw =
        "HTTP/1.1 404 Not Found\r\n"
        "Server: teko\r\n"
        "\r\n";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    int rc = teko_http_parse_response(buf, len, &resp);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL(404, resp.status_code);
    TEST_ASSERT_EQUAL_STRING("Not Found", resp.reason_phrase);
    TEST_ASSERT_NULL(resp.body);
    TEST_ASSERT_EQUAL(0, (int)resp.body_len);
    free(buf);
}

void test_http_parse_response_null_args(void) {
    char dummy[4] = {0};
    TekoHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_parse_response(NULL, 0, &resp));
    TEST_ASSERT_EQUAL(TEKO_HTTP_ERR_NULL_ARG, teko_http_parse_response(dummy, 0, NULL));
}

/* =========================================================================
 * Round-trip: build then parse
 * ====================================================================== */

void test_http_request_round_trip(void) {
    /* Build a request, then parse the output, and verify symmetry. */
    TekoHttpHeader hdrs[2];
    hdrs[0].name  = "Host";
    hdrs[0].value = "roundtrip.test";
    hdrs[1].name  = "X-Custom";
    hdrs[1].value = "teko";

    const char *body = "round-trip-body";
    size_t body_len = strlen(body);

    char  *built = NULL;
    size_t built_len = 0;
    int rc = teko_http_build_request("POST", "/rt", hdrs, 2, body, body_len, &built, &built_len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(built);

    /* Parse back. */
    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    rc = teko_http_parse_request(built, built_len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL_STRING("POST", req.method);
    TEST_ASSERT_EQUAL_STRING("/rt", req.path);
    /* host + x-custom + auto content-length = 3 headers */
    TEST_ASSERT_EQUAL(3, (int)req.header_count);
    TEST_ASSERT_NOT_NULL(req.body);
    TEST_ASSERT_EQUAL(body_len, req.body_len);
    TEST_ASSERT_EQUAL_MEMORY(body, req.body, body_len);
    free(built);
}

void test_http_response_round_trip(void) {
    TekoHttpHeader hdrs[1];
    hdrs[0].name  = "Content-Type";
    hdrs[0].value = "application/json";

    const char *body = "{\"ok\":true}";
    size_t body_len = strlen(body);

    char  *built = NULL;
    size_t built_len = 0;
    int rc = teko_http_build_response(200, "OK", hdrs, 1, body, body_len, &built, &built_len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(built);

    TekoHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    rc = teko_http_parse_response(built, built_len, &resp);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL(200, resp.status_code);
    /* content-type + auto content-length = 2 */
    TEST_ASSERT_EQUAL(2, (int)resp.header_count);
    TEST_ASSERT_NOT_NULL(resp.body);
    TEST_ASSERT_EQUAL(body_len, resp.body_len);
    TEST_ASSERT_EQUAL_MEMORY(body, resp.body, body_len);
    free(built);
}

/* =========================================================================
 * Chunked encode / decode
 * ====================================================================== */

void test_http_chunked_encode_decode_round_trip(void) {
    const char *src = "Hello, chunked world!";
    size_t src_len = strlen(src);

    char  *encoded = NULL;
    size_t enc_len = 0;
    int rc = teko_http_chunked_encode(src, src_len, &encoded, &enc_len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(encoded);
    TEST_ASSERT_GREATER_THAN(0, (int)enc_len);

    /* The encoded form must start with a hex size. */
    /* "Hello, chunked world!" = 21 bytes = 0x15 */
    TEST_ASSERT_EQUAL('1', encoded[0]);
    TEST_ASSERT_EQUAL('5', encoded[1]);
    TEST_ASSERT_EQUAL('\r', encoded[2]);
    TEST_ASSERT_EQUAL('\n', encoded[3]);

    /* Decode it back. */
    char  *decoded = NULL;
    size_t dec_len = 0;
    rc = teko_http_chunked_decode(encoded, enc_len, &decoded, &dec_len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(decoded);
    TEST_ASSERT_EQUAL(src_len, dec_len);
    TEST_ASSERT_EQUAL_MEMORY(src, decoded, src_len);

    free(encoded);
    free(decoded);
}

void test_http_chunked_encode_empty(void) {
    char  *out = NULL;
    size_t len = 0;
    int rc = teko_http_chunked_encode("", 0, &out, &len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(out);
    /* Just the terminal chunk: "0\r\n\r\n" = 5 bytes. */
    TEST_ASSERT_EQUAL(5, (int)len);
    TEST_ASSERT_EQUAL_MEMORY("0\r\n\r\n", out, 5);
    free(out);
}

void test_http_chunked_decode_multi_chunk(void) {
    /* Multiple chunks: "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n" */
    const char *enc = "5\r\nHello\r\n6\r\n World\r\n0\r\n\r\n";
    size_t enc_len = strlen(enc);
    char  *out = NULL;
    size_t out_len = 0;
    int rc = teko_http_chunked_decode(enc, enc_len, &out, &out_len);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(out);
    TEST_ASSERT_EQUAL(11, (int)out_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello World", out, 11);
    free(out);
}

void test_http_chunked_decode_missing_crlf(void) {
    /* Missing CRLF after chunk size. */
    const char *enc = "5Hello\r\n0\r\n\r\n";
    char  *out = NULL;
    size_t out_len = 0;
    int rc = teko_http_chunked_decode(enc, strlen(enc), &out, &out_len);
    TEST_ASSERT_NOT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NULL(out);
}

void test_http_chunked_decode_bad_hex_digit(void) {
    /* 'g' is not a valid hex digit. */
    const char *enc = "g\r\nhello\r\n0\r\n\r\n";
    char  *out = NULL;
    size_t out_len = 0;
    int rc = teko_http_chunked_decode(enc, strlen(enc), &out, &out_len);
    TEST_ASSERT_NOT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NULL(out);
}

void test_http_chunked_decode_oversized_hex(void) {
    /* 17 hex digits in chunk size = exceeds TEKO_HTTP_MAX_CHUNK_SIZE_DIGITS (16). */
    const char *enc = "11111111111111111\r\ndata\r\n0\r\n\r\n"; /* 17 '1's */
    char  *out = NULL;
    size_t out_len = 0;
    int rc = teko_http_chunked_decode(enc, strlen(enc), &out, &out_len);
    TEST_ASSERT_NOT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NULL(out);
}

void test_http_chunked_decode_chunk_overflow(void) {
    /*
     * Craft a chunk-size that is valid hex but represents a value > TEKO_HTTP_BODY_MAX.
     * TEKO_HTTP_BODY_MAX = 64 MiB = 0x4000000.  Use 0x10000000 (256 MiB) which is > cap.
     * The encoded data doesn't need to actually be that long — we just need the
     * parser to reject the size before attempting the copy.
     */
    const char *enc = "10000000\r\nX\r\n0\r\n\r\n";  /* chunk-size = 256 MiB */
    char  *out = NULL;
    size_t out_len = 0;
    int rc = teko_http_chunked_decode(enc, strlen(enc), &out, &out_len);
    TEST_ASSERT_NOT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NULL(out);
}

/* =========================================================================
 * Chunked inside parse_request (in-place decode)
 * ====================================================================== */

void test_http_parse_request_chunked_body(void) {
    const char *raw =
        "POST /upload HTTP/1.1\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "5\r\nHello\r\n"
        "6\r\n World\r\n"
        "0\r\n\r\n";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(req.body);
    TEST_ASSERT_EQUAL(11, (int)req.body_len);
    TEST_ASSERT_EQUAL_MEMORY("Hello World", req.body, 11);
    free(buf);
}

void test_http_parse_response_chunked_body(void) {
    const char *raw =
        "HTTP/1.1 200 OK\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\ndata\r\n"
        "0\r\n\r\n";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpResponse resp;
    memset(&resp, 0, sizeof(resp));
    int rc = teko_http_parse_response(buf, len, &resp);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_NOT_NULL(resp.body);
    TEST_ASSERT_EQUAL(4, (int)resp.body_len);
    TEST_ASSERT_EQUAL_MEMORY("data", resp.body, 4);
    free(buf);
}

/* =========================================================================
 * find_header
 * ====================================================================== */

void test_http_find_header_case_insensitive(void) {
    TekoHttpHeader hdrs[3];
    /* These are const-cast for test setup — real usage is parse output. */
    hdrs[0].name  = "Content-Type";
    hdrs[0].value = "text/html";
    hdrs[1].name  = "X-Custom";
    hdrs[1].value = "foo";
    hdrs[2].name  = "content-length";
    hdrs[2].value = "42";

    const TekoHttpHeader *h;

    h = teko_http_find_header(hdrs, 3, "content-type");
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_STRING("text/html", h->value);

    h = teko_http_find_header(hdrs, 3, "CONTENT-TYPE");
    TEST_ASSERT_NOT_NULL(h);

    h = teko_http_find_header(hdrs, 3, "Content-Length");
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_EQUAL_STRING("42", h->value);

    h = teko_http_find_header(hdrs, 3, "X-Missing");
    TEST_ASSERT_NULL(h);
}

void test_http_find_header_null_args(void) {
    TekoHttpHeader hdrs[1];
    hdrs[0].name  = "X";
    hdrs[0].value = "y";
    TEST_ASSERT_NULL(teko_http_find_header(NULL, 1, "X"));
    TEST_ASSERT_NULL(teko_http_find_header(hdrs, 1, NULL));
}

/* =========================================================================
 * Header value whitespace trimming
 * ====================================================================== */

void test_http_parse_request_header_lws_trim(void) {
    /* Value has leading and trailing LWS (RFC 7230 §3.2.6 field-value trimming). */
    const char *raw =
        "GET / HTTP/1.1\r\n"
        "X-Padded:   value with spaces   \r\n"
        "\r\n";
    size_t len;
    char *buf = make_buf(raw, &len);
    TEST_ASSERT_NOT_NULL(buf);

    TekoHttpRequest req;
    memset(&req, 0, sizeof(req));
    int rc = teko_http_parse_request(buf, len, &req);
    TEST_ASSERT_EQUAL(TEKO_HTTP_OK, rc);
    TEST_ASSERT_EQUAL(1, (int)req.header_count);
    TEST_ASSERT_EQUAL_STRING("value with spaces", req.headers[0].value);
    free(buf);
}

/* No Unity runner here: individual test functions are declared extern in test_main.c
 * and called via RUN_TEST() in that file's UNITY_BEGIN/END block. */
