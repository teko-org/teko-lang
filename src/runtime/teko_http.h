#ifndef TEKO_HTTP_H
#define TEKO_HTTP_H

/*
 * Phase 19 (Networking) — HTTP/1.1 request/response codec.
 * Pure byte-buffer unit: build request/response bytes; parse request/response bytes.
 * No sockets, no frontend wiring this wave (integration = HTTP-INT/HTTP-SRV, Wave 1).
 *
 * OP_CALL_RUNTIME id range 80–99 reserved for http (no emission/frontend wiring this wave).
 *
 * Design contract:
 *   - Freestanding-safe: only malloc/free/memcpy/strlen/memcmp.  No snprintf/strtoll/sscanf.
 *   - MSVC-portable: no C23 auto/nullptr; no __attribute__; TEKO_PACKED not used here.
 *   - Attacker-controlled input: HTTP bytes are untrusted.  Every length is bounds-checked
 *     before any copy.  Integer arithmetic uses overflow-safe patterns (see TEKO_HTTP_BODY_MAX).
 *   - Ownership: build_* return a heap buffer the caller frees with free().
 *                parse_* write into caller-supplied structs; body/value slices point into
 *                the caller-supplied parse buffer (zero-copy) — do NOT free them separately.
 *   - Error handling: all functions return TEKO_HTTP_OK (0) on success, a negative error
 *     code on failure.  Partial output is never produced: on error the output pointer is
 *     set to NULL / struct fields are left at their initial (zeroed) values.
 *   - No format-string paths: method/path/headers are treated as opaque byte sequences.
 *   - obs-fold (header line folding per obsolete RFC 2616) is REJECTED (security risk).
 *   - Duplicate Content-Length headers are REJECTED (request-smuggling guard).
 */

#include <stddef.h>  /* size_t */

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * Limits (compile-time caps; checked before every copy / allocation)
 * ---------------------------------------------------------------------- */

/* Maximum number of headers in a single request or response. */
#define TEKO_HTTP_MAX_HEADERS       64

/* Maximum length of a header name (bytes, excluding NUL). */
#define TEKO_HTTP_MAX_HEADER_NAME   256

/* Maximum length of a header value (bytes, excluding NUL). */
#define TEKO_HTTP_MAX_HEADER_VALUE  8192

/* Maximum HTTP method length (bytes, excluding NUL). */
#define TEKO_HTTP_MAX_METHOD        16

/* Maximum request path length (bytes, excluding NUL). */
#define TEKO_HTTP_MAX_PATH          8192

/* Maximum HTTP version string length (bytes, excluding NUL).  "HTTP/1.1" = 8. */
#define TEKO_HTTP_MAX_VERSION       16

/* Maximum body size in bytes.  Guard against integer overflow: any Content-Length or
 * chunk-sum that exceeds this is rejected (TEKO_HTTP_ERR_BODY_TOO_LARGE).
 * Set to 64 MiB — sufficient for API use; large uploads belong in streaming. */
#define TEKO_HTTP_BODY_MAX          ((size_t)64 * 1024 * 1024)

/* Maximum size of a single chunk-size hex string (digits), excluding CRLF. */
#define TEKO_HTTP_MAX_CHUNK_SIZE_DIGITS  16

/* Maximum total encoded (chunked) output size: body + chunk framing.
 * Framing overhead per chunk: up to 16 hex digits + "\r\n" + "\r\n" = 20 bytes.
 * We add a fixed 64-byte margin for the terminal "0\r\n\r\n" and any trailers. */
#define TEKO_HTTP_CHUNKED_OVERHEAD  64

/* -------------------------------------------------------------------------
 * Return codes
 * ---------------------------------------------------------------------- */

#define TEKO_HTTP_OK                      0
#define TEKO_HTTP_ERR_NULL_ARG          (-1)  /* required pointer was NULL */
#define TEKO_HTTP_ERR_ALLOC             (-2)  /* malloc returned NULL */
#define TEKO_HTTP_ERR_TOO_MANY_HEADERS  (-3)  /* header count exceeds TEKO_HTTP_MAX_HEADERS */
#define TEKO_HTTP_ERR_NAME_TOO_LONG     (-4)  /* header name exceeds TEKO_HTTP_MAX_HEADER_NAME */
#define TEKO_HTTP_ERR_VALUE_TOO_LONG    (-5)  /* header value exceeds TEKO_HTTP_MAX_HEADER_VALUE */
#define TEKO_HTTP_ERR_METHOD_TOO_LONG   (-6)  /* method exceeds TEKO_HTTP_MAX_METHOD */
#define TEKO_HTTP_ERR_PATH_TOO_LONG     (-7)  /* path exceeds TEKO_HTTP_MAX_PATH */
#define TEKO_HTTP_ERR_MALFORMED         (-8)  /* missing CRLF, bad request-line, etc. */
#define TEKO_HTTP_ERR_BODY_TOO_LARGE    (-9)  /* Content-Length or chunk sum > TEKO_HTTP_BODY_MAX */
#define TEKO_HTTP_ERR_BAD_CHUNK        (-10)  /* invalid chunk-size hex or missing CRLF */
#define TEKO_HTTP_ERR_OVERFLOW         (-11)  /* integer overflow in size computation */
#define TEKO_HTTP_ERR_SMUGGLING        (-12)  /* duplicate Content-Length (request smuggling guard) */
#define TEKO_HTTP_ERR_OBS_FOLD         (-13)  /* obs-fold (folded header line) rejected */
#define TEKO_HTTP_ERR_TRUNCATED        (-14)  /* input ended before the message was complete */

/* -------------------------------------------------------------------------
 * Data structures
 * ---------------------------------------------------------------------- */

/*
 * A single HTTP header name/value pair.
 * name and value are NUL-terminated C strings.
 * When returned by a parse function they point into the parse buffer supplied
 * by the caller — do NOT free them.
 * When supplied by the caller to a build function they are read-only inputs.
 */
typedef struct {
    const char *name;   /* header name (case-preserved from source; compared case-insensitively) */
    const char *value;  /* header value (LWS-trimmed on parse) */
} TekoHttpHeader;

/*
 * Parsed HTTP request.
 * Filled in by teko_http_parse_request().
 * All string fields point into the parse buffer supplied by the caller.
 * Zero-initialize before calling parse (calloc / = {0}).
 */
typedef struct {
    const char    *method;                          /* e.g. "GET", "POST" */
    const char    *path;                            /* request target, e.g. "/foo?bar=1" */
    const char    *version;                         /* e.g. "HTTP/1.1" */
    TekoHttpHeader headers[TEKO_HTTP_MAX_HEADERS];  /* parsed headers */
    size_t         header_count;
    const char    *body;     /* points into parse buffer (may be NULL if no body) */
    size_t         body_len; /* 0 if no body */
} TekoHttpRequest;

/*
 * Parsed HTTP response.
 * Filled in by teko_http_parse_response().
 * All string fields point into the parse buffer supplied by the caller.
 * Zero-initialize before calling parse.
 */
typedef struct {
    const char    *version;                         /* e.g. "HTTP/1.1" */
    int            status_code;                     /* e.g. 200, 404 */
    const char    *reason_phrase;                   /* e.g. "OK", "Not Found" */
    TekoHttpHeader headers[TEKO_HTTP_MAX_HEADERS];
    size_t         header_count;
    const char    *body;
    size_t         body_len;
} TekoHttpResponse;

/* -------------------------------------------------------------------------
 * Build functions (return a heap-allocated buffer; caller calls free())
 * ---------------------------------------------------------------------- */

/*
 * teko_http_build_request
 *   Build a complete HTTP/1.1 request byte string.
 *
 *   method      NUL-terminated, e.g. "GET"
 *   path        NUL-terminated, e.g. "/index.html"
 *   headers     array of `header_count` name/value pairs (may be NULL if header_count == 0)
 *   header_count number of entries in `headers`
 *   body        optional body bytes (may be NULL if body_len == 0)
 *   body_len    length of body in bytes
 *   out_buf     receives a pointer to the allocated buffer on success; set to NULL on error
 *   out_len     receives the length of the buffer (excluding NUL terminator) on success
 *
 *   Returns TEKO_HTTP_OK on success, a negative error code on failure.
 *   On success *out_buf points to a NUL-terminated heap buffer of *out_len bytes.
 *   The caller must free(*out_buf).
 *
 *   A "Content-Length: <body_len>" header is appended automatically when body_len > 0
 *   and no Content-Length header is already present in `headers`.
 */
int teko_http_build_request(
        const char           *method,
        const char           *path,
        const TekoHttpHeader *headers,
        size_t                header_count,
        const char           *body,
        size_t                body_len,
        char                **out_buf,
        size_t               *out_len);

/*
 * teko_http_build_response
 *   Build a complete HTTP/1.1 response byte string.
 *
 *   status_code    e.g. 200
 *   reason_phrase  NUL-terminated, e.g. "OK" (may be NULL → empty reason phrase)
 *   headers        array of `header_count` name/value pairs (may be NULL if 0)
 *   header_count   number of entries in `headers`
 *   body           optional body bytes (may be NULL if body_len == 0)
 *   body_len       length of body in bytes
 *   out_buf        receives a pointer to the allocated buffer on success
 *   out_len        receives the buffer length on success
 *
 *   Returns TEKO_HTTP_OK on success, a negative error code on failure.
 *   Caller must free(*out_buf).
 *
 *   Content-Length is auto-appended when body_len > 0 and not already present.
 */
int teko_http_build_response(
        int                   status_code,
        const char           *reason_phrase,
        const TekoHttpHeader *headers,
        size_t                header_count,
        const char           *body,
        size_t                body_len,
        char                **out_buf,
        size_t               *out_len);

/* -------------------------------------------------------------------------
 * Parse functions (zero-copy into caller-supplied structs)
 * ---------------------------------------------------------------------- */

/*
 * teko_http_parse_request
 *   Parse `len` bytes at `buf` as an HTTP/1.1 request.
 *
 *   buf   input bytes (MUST be a writable, NUL-terminated mutable buffer — the parser
 *         NUL-terminates tokens in-place to produce zero-copy slices)
 *   len   number of bytes to parse (not counting any trailing NUL the caller may have added)
 *   req   output struct (caller zero-initialises before the call)
 *
 *   Returns TEKO_HTTP_OK on success; a negative error code on failure.
 *   On success all pointer fields in *req point into buf.
 *   The body is always in buf[body_offset .. body_offset+body_len-1].
 *
 *   Chunked Transfer-Encoding: if the request uses chunked TE, the body is
 *   decoded in-place into buf (the decoded body is always <= the encoded body in size).
 *   req->body_len reflects the decoded length.
 *
 *   Rejects: obs-fold; duplicate Content-Length; Content-Length > TEKO_HTTP_BODY_MAX;
 *   chunk sum > TEKO_HTTP_BODY_MAX; missing terminal CRLF.
 */
int teko_http_parse_request(
        char           *buf,
        size_t          len,
        TekoHttpRequest *req);

/*
 * teko_http_parse_response
 *   Parse `len` bytes at `buf` as an HTTP/1.1 response.
 *   Same contract as teko_http_parse_request but fills *resp.
 *
 *   buf   writable, NUL-terminated mutable buffer
 *   len   number of bytes to parse
 *   resp  output struct (caller zero-initialises)
 */
int teko_http_parse_response(
        char            *buf,
        size_t           len,
        TekoHttpResponse *resp);

/* -------------------------------------------------------------------------
 * Chunked Transfer-Encoding encode / decode
 * ---------------------------------------------------------------------- */

/*
 * teko_http_chunked_encode
 *   Encode `src_len` bytes at `src` into chunked transfer encoding.
 *   Produces a single chunk + the terminal "0\r\n\r\n".
 *
 *   src        input bytes
 *   src_len    number of bytes to encode (must be <= TEKO_HTTP_BODY_MAX)
 *   out_buf    receives a pointer to the allocated output buffer on success
 *   out_len    receives the length of the encoded output
 *
 *   Returns TEKO_HTTP_OK on success; the caller frees *out_buf.
 *   Returns TEKO_HTTP_ERR_BODY_TOO_LARGE if src_len > TEKO_HTTP_BODY_MAX.
 *   Returns TEKO_HTTP_ERR_OVERFLOW if the framing arithmetic would overflow.
 */
int teko_http_chunked_encode(
        const char *src,
        size_t      src_len,
        char      **out_buf,
        size_t     *out_len);

/*
 * teko_http_chunked_decode
 *   Decode chunked-encoded bytes at `src` (length `src_len`) into a fresh heap buffer.
 *   Rejects: non-hex chunk size digits; chunk size > TEKO_HTTP_BODY_MAX; cumulative
 *   decoded size > TEKO_HTTP_BODY_MAX; missing CRLF after chunk-size or chunk-data;
 *   chunk-size hex string > TEKO_HTTP_MAX_CHUNK_SIZE_DIGITS digits.
 *
 *   src        encoded input (does not need to be NUL-terminated)
 *   src_len    byte count of encoded input
 *   out_buf    receives a pointer to the decoded output on success (heap; caller frees)
 *   out_len    receives decoded length
 *
 *   Returns TEKO_HTTP_OK on success; a negative error code on failure.
 */
int teko_http_chunked_decode(
        const char *src,
        size_t      src_len,
        char      **out_buf,
        size_t     *out_len);

/* -------------------------------------------------------------------------
 * Utility: case-insensitive header lookup
 * ---------------------------------------------------------------------- */

/*
 * teko_http_find_header
 *   Search `headers[0..count-1]` for a header whose name matches `name`
 *   case-insensitively.  Returns a pointer to the matching TekoHttpHeader on
 *   the first match, or NULL if not found.
 */
const TekoHttpHeader *teko_http_find_header(
        const TekoHttpHeader *headers,
        size_t                count,
        const char           *name);

#ifdef __cplusplus
}
#endif

#endif /* TEKO_HTTP_H */
