/*
 * Phase 19 (Networking) — HTTP/1.1 request/response codec.
 * Pure byte-buffer unit.  No sockets, no snprintf, no strtoll.
 * Freestanding-safe: only malloc/free/memcpy/memcmp/strlen.
 *
 * OP_CALL_RUNTIME id range 80–99 reserved for http (no emission this wave).
 *
 * SAST notes:
 *   - All buffer size arithmetic uses overflow-safe helpers (safe_add_sz, safe_mul_sz).
 *   - Every memcpy is preceded by a bounds check against the allocated or remaining size.
 *   - Content-Length is read as a size_t via a hand-rolled digit parser — no strtoll.
 *   - Duplicate Content-Length → TEKO_HTTP_ERR_SMUGGLING.
 *   - obs-fold (SP/HT at line start after header) → TEKO_HTTP_ERR_OBS_FOLD.
 *   - Chunk-size hex strings > TEKO_HTTP_MAX_CHUNK_SIZE_DIGITS digits → TEKO_HTTP_ERR_BAD_CHUNK.
 *   - Cumulative chunk body > TEKO_HTTP_BODY_MAX → TEKO_HTTP_ERR_BODY_TOO_LARGE.
 *   - No format-string paths anywhere.
 */

#include "teko_http.h"
#include <stdlib.h>   /* malloc, free */
#include <string.h>   /* memcpy, memcmp, strlen */

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
 * Overflow-safe size_t multiplication.
 * Sets *out = a * b and returns 1 on success, 0 on overflow (or if result == 0 from a*b=0 is fine).
 */
static int safe_mul_sz(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > (size_t)-1 / a) return 0;
    *out = a * b;
    return 1;
}

/* Convert ASCII letter to lowercase (portable, no ctype). */
static unsigned char to_lower_ascii(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + 32);
    return c;
}

/*
 * Case-insensitive ASCII comparison of two NUL-terminated strings.
 * Returns 1 if equal, 0 otherwise.
 */
static int icase_eq(const char *a, const char *b) {
    if (!a || !b) return (a == b);
    while (*a && *b) {
        if (to_lower_ascii((unsigned char)*a) != to_lower_ascii((unsigned char)*b)) return 0;
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

/*
 * Case-insensitive comparison for a NUL-terminated `name` against a
 * length-bounded string `s` of `slen` bytes (not NUL-terminated).
 * Returns 1 if equal.
 */
static int icase_eq_n(const char *name, const char *s, size_t slen) {
    size_t nlen = strlen(name);
    if (nlen != slen) return 0;
    for (size_t i = 0; i < slen; i++) {
        if (to_lower_ascii((unsigned char)name[i]) !=
            to_lower_ascii((unsigned char)s[i])) return 0;
    }
    return 1;
}

/*
 * Append a NUL-terminated string into a dynamic buffer.
 * *buf   pointer to heap buffer (may be reallocated)
 * *pos   current write position (bytes written so far)
 * *cap   current allocated capacity
 * s      string to append
 * slen   byte count of s (excluding NUL)
 *
 * Returns 1 on success, 0 on allocation failure or overflow.
 */
static int buf_append(char **buf, size_t *pos, size_t *cap, const char *s, size_t slen) {
    if (slen == 0) return 1;
    size_t needed;
    if (!safe_add_sz(*pos, slen, &needed)) return 0;
    size_t needed_nul;
    if (!safe_add_sz(needed, 1, &needed_nul)) return 0;  /* +1 for final NUL */
    if (needed_nul > *cap) {
        /* Grow: at least double, at least needed_nul. */
        size_t newcap = *cap;
        if (newcap == 0) newcap = 256;
        while (newcap < needed_nul) {
            size_t doubled;
            if (!safe_mul_sz(newcap, 2, &doubled)) { newcap = needed_nul; break; }
            newcap = doubled;
        }
        char *nb = (char *)malloc(newcap);
        if (!nb) return 0;
        if (*pos > 0) memcpy(nb, *buf, *pos);
        free(*buf);
        *buf = nb;
        *cap = newcap;
    }
    memcpy(*buf + *pos, s, slen);
    *pos += slen;
    (*buf)[*pos] = '\0';
    return 1;
}

/*
 * Append a size_t integer as decimal ASCII into the dynamic buffer.
 * Hand-rolled — no snprintf.
 */
static int buf_append_size_t(char **buf, size_t *pos, size_t *cap, size_t v) {
    char tmp[24];  /* size_t max = 20 digits on 64-bit */
    int i = (int)sizeof(tmp);
    tmp[--i] = '\0';
    if (v == 0) { tmp[--i] = '0'; }
    else {
        while (v > 0) { tmp[--i] = (char)('0' + (v % 10)); v /= 10; }
    }
    return buf_append(buf, pos, cap, tmp + i, sizeof(tmp) - (size_t)i - 1);
}

/*
 * Append a 3-digit HTTP status code as decimal ASCII.
 */
static int buf_append_int3(char **buf, size_t *pos, size_t *cap, int v) {
    char tmp[8];
    int av = (v < 0) ? -v : v;
    tmp[0] = (char)('0' + ((av / 100) % 10));
    tmp[1] = (char)('0' + ((av / 10) % 10));
    tmp[2] = (char)('0' + (av % 10));
    tmp[3] = '\0';
    return buf_append(buf, pos, cap, tmp, 3);
}

/* Check if there is a "Content-Length" header already in the array. */
static int has_content_length(const TekoHttpHeader *headers, size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (icase_eq(headers[i].name, "Content-Length")) return 1;
    }
    return 0;
}

/* =========================================================================
 * Build helpers (shared between request and response builders)
 * ====================================================================== */

/*
 * Serialize headers + body into a dynamic buffer.
 * start_line must already be in *buf / *pos / *cap.
 * Automatically appends "Content-Length: N\r\n" if body_len > 0 and no
 * Content-Length header is in the provided array.
 */
static int build_headers_and_body(
        char               **buf,
        size_t              *pos,
        size_t              *cap,
        const TekoHttpHeader *headers,
        size_t               header_count,
        const char          *body,
        size_t               body_len)
{
    /* Validate header names and values before writing anything. */
    for (size_t i = 0; i < header_count; i++) {
        if (!headers[i].name || !headers[i].value) return TEKO_HTTP_ERR_NULL_ARG;
        if (strlen(headers[i].name)  > TEKO_HTTP_MAX_HEADER_NAME)  return TEKO_HTTP_ERR_NAME_TOO_LONG;
        if (strlen(headers[i].value) > TEKO_HTTP_MAX_HEADER_VALUE) return TEKO_HTTP_ERR_VALUE_TOO_LONG;
    }
    if (body_len > TEKO_HTTP_BODY_MAX) return TEKO_HTTP_ERR_BODY_TOO_LARGE;

    /* Write provided headers. */
    for (size_t i = 0; i < header_count; i++) {
        size_t nlen = strlen(headers[i].name);
        size_t vlen = strlen(headers[i].value);
        if (!buf_append(buf, pos, cap, headers[i].name, nlen)) return TEKO_HTTP_ERR_ALLOC;
        if (!buf_append(buf, pos, cap, ": ", 2))               return TEKO_HTTP_ERR_ALLOC;
        if (!buf_append(buf, pos, cap, headers[i].value, vlen)) return TEKO_HTTP_ERR_ALLOC;
        if (!buf_append(buf, pos, cap, "\r\n", 2))              return TEKO_HTTP_ERR_ALLOC;
    }

    /* Auto Content-Length when body present and not already set. */
    if (body_len > 0 && !has_content_length(headers, header_count)) {
        if (!buf_append(buf, pos, cap, "Content-Length: ", 16)) return TEKO_HTTP_ERR_ALLOC;
        if (!buf_append_size_t(buf, pos, cap, body_len))        return TEKO_HTTP_ERR_ALLOC;
        if (!buf_append(buf, pos, cap, "\r\n", 2))              return TEKO_HTTP_ERR_ALLOC;
    }

    /* Blank line separating headers from body. */
    if (!buf_append(buf, pos, cap, "\r\n", 2)) return TEKO_HTTP_ERR_ALLOC;

    /* Body. */
    if (body && body_len > 0) {
        if (!buf_append(buf, pos, cap, body, body_len)) return TEKO_HTTP_ERR_ALLOC;
    }
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: build_request
 * ====================================================================== */

int teko_http_build_request(
        const char           *method,
        const char           *path,
        const TekoHttpHeader *headers,
        size_t                header_count,
        const char           *body,
        size_t                body_len,
        char                **out_buf,
        size_t               *out_len)
{
    if (!method || !path || !out_buf || !out_len) return TEKO_HTTP_ERR_NULL_ARG;
    if (header_count > 0 && !headers)            return TEKO_HTTP_ERR_NULL_ARG;
    if (body_len > 0 && !body)                   return TEKO_HTTP_ERR_NULL_ARG;

    *out_buf = NULL;
    *out_len = 0;

    size_t mlen = strlen(method);
    size_t plen = strlen(path);
    if (mlen > TEKO_HTTP_MAX_METHOD) return TEKO_HTTP_ERR_METHOD_TOO_LONG;
    if (plen > TEKO_HTTP_MAX_PATH)   return TEKO_HTTP_ERR_PATH_TOO_LONG;
    if (header_count > TEKO_HTTP_MAX_HEADERS) return TEKO_HTTP_ERR_TOO_MANY_HEADERS;

    char  *buf  = NULL;
    size_t pos  = 0;
    size_t cap  = 0;
    int    rc;

    /* Request-line: METHOD SP path SP HTTP/1.1 CRLF */
    if (!buf_append(&buf, &pos, &cap, method, mlen)) { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    if (!buf_append(&buf, &pos, &cap, " ", 1))       { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    if (!buf_append(&buf, &pos, &cap, path, plen))   { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    if (!buf_append(&buf, &pos, &cap, " HTTP/1.1\r\n", 11)) { free(buf); return TEKO_HTTP_ERR_ALLOC; }

    rc = build_headers_and_body(&buf, &pos, &cap, headers, header_count, body, body_len);
    if (rc != TEKO_HTTP_OK) { free(buf); return rc; }

    *out_buf = buf;
    *out_len = pos;
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: build_response
 * ====================================================================== */

int teko_http_build_response(
        int                   status_code,
        const char           *reason_phrase,
        const TekoHttpHeader *headers,
        size_t                header_count,
        const char           *body,
        size_t                body_len,
        char                **out_buf,
        size_t               *out_len)
{
    if (!out_buf || !out_len) return TEKO_HTTP_ERR_NULL_ARG;
    if (header_count > 0 && !headers) return TEKO_HTTP_ERR_NULL_ARG;
    if (body_len > 0 && !body)        return TEKO_HTTP_ERR_NULL_ARG;
    if (header_count > TEKO_HTTP_MAX_HEADERS) return TEKO_HTTP_ERR_TOO_MANY_HEADERS;

    *out_buf = NULL;
    *out_len = 0;

    const char *rp = reason_phrase ? reason_phrase : "";
    size_t rplen = strlen(rp);
    if (rplen > TEKO_HTTP_MAX_HEADER_VALUE) return TEKO_HTTP_ERR_VALUE_TOO_LONG;

    char  *buf = NULL;
    size_t pos = 0;
    size_t cap = 0;
    int    rc;

    /* Status-line: HTTP/1.1 SP 3digits SP reason CRLF */
    if (!buf_append(&buf, &pos, &cap, "HTTP/1.1 ", 9))   { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    if (!buf_append_int3(&buf, &pos, &cap, status_code)) { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    if (!buf_append(&buf, &pos, &cap, " ", 1))           { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    if (rplen > 0) {
        if (!buf_append(&buf, &pos, &cap, rp, rplen))    { free(buf); return TEKO_HTTP_ERR_ALLOC; }
    }
    if (!buf_append(&buf, &pos, &cap, "\r\n", 2))        { free(buf); return TEKO_HTTP_ERR_ALLOC; }

    rc = build_headers_and_body(&buf, &pos, &cap, headers, header_count, body, body_len);
    if (rc != TEKO_HTTP_OK) { free(buf); return rc; }

    *out_buf = buf;
    *out_len = pos;
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Parse helpers
 * ====================================================================== */

/*
 * Find the next CRLF in buf[pos..len-1].
 * Returns the index of '\r' on success, or (size_t)-1 if not found.
 * Also rejects if '\n' is found without a preceding '\r' (bare LF) — we treat
 * that as a format error (strict parse).
 */
static size_t find_crlf(const char *buf, size_t start, size_t len) {
    for (size_t i = start; i + 1 < len; i++) {
        if (buf[i] == '\r' && buf[i + 1] == '\n') return i;
    }
    return (size_t)-1;
}

/*
 * Hand-rolled size_t parser from ASCII decimal.
 * Rejects empty string, non-digit characters, leading zeros (except "0"),
 * and values > TEKO_HTTP_BODY_MAX.
 * Returns 1 on success, 0 on failure.
 */
static int parse_content_length(const char *s, size_t *out) {
    if (!s || *s == '\0') return 0;
    /* Skip leading whitespace. */
    while (*s == ' ' || *s == '\t') s++;
    if (*s < '0' || *s > '9') return 0;
    size_t val = 0;
    while (*s >= '0' && *s <= '9') {
        size_t digit = (size_t)(*s - '0');
        /* Check: val * 10 + digit <= TEKO_HTTP_BODY_MAX */
        if (val > TEKO_HTTP_BODY_MAX / 10) return 0;          /* multiply overflow */
        val *= 10;
        if (val > TEKO_HTTP_BODY_MAX - digit) return 0;       /* add overflow */
        val += digit;
        s++;
    }
    /* Allow trailing whitespace, reject other trailing chars. */
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '\0') return 0;
    *out = val;
    return 1;
}

/*
 * Parse headers from buf[pos..end_of_headers).
 * NUL-terminates name and value in-place.
 * Returns TEKO_HTTP_OK on success.
 *
 * `cl_seen`  is set to 1 if Content-Length was found, *cl to its value.
 * `te_chunked` is set to 1 if Transfer-Encoding: chunked was found.
 */
static int parse_headers(
        char            *buf,
        size_t           start,     /* index of first header byte */
        size_t           hdr_end,   /* index just past the last header byte (before blank line) */
        TekoHttpHeader  *headers,
        size_t          *hcount,
        int             *cl_seen,
        size_t          *cl,
        int             *te_chunked)
{
    size_t pos = start;
    *hcount     = 0;
    *cl_seen    = 0;
    *cl         = 0;
    *te_chunked = 0;

    while (pos < hdr_end) {
        /* obs-fold: a line starting with SP or HT after the first header → reject */
        if ((buf[pos] == ' ' || buf[pos] == '\t') && *hcount > 0) {
            return TEKO_HTTP_ERR_OBS_FOLD;
        }
        /* Find the end of this header line. */
        size_t crlf = find_crlf(buf, pos, hdr_end + 2); /* +2: the blank-line CRLF is at hdr_end */
        if (crlf == (size_t)-1) return TEKO_HTTP_ERR_MALFORMED;
        if (crlf == pos) break;  /* blank line — end of headers */

        /* Find the colon separating name from value. */
        size_t colon = pos;
        while (colon < crlf && buf[colon] != ':') colon++;
        if (colon == crlf) return TEKO_HTTP_ERR_MALFORMED; /* no colon */

        size_t name_len = colon - pos;
        if (name_len == 0 || name_len > TEKO_HTTP_MAX_HEADER_NAME) return TEKO_HTTP_ERR_NAME_TOO_LONG;

        /* NUL-terminate name. */
        buf[colon] = '\0';
        char *name = buf + pos;

        /* Value starts after colon; trim LWS. */
        size_t vstart = colon + 1;
        while (vstart < crlf && (buf[vstart] == ' ' || buf[vstart] == '\t')) vstart++;
        /* NUL-terminate line at CRLF. */
        buf[crlf] = '\0';
        char *value = buf + vstart;
        /* Trim trailing LWS. */
        char *vend = buf + crlf;
        while (vend > value && (*(vend - 1) == ' ' || *(vend - 1) == '\t')) { vend--; *vend = '\0'; }

        size_t vlen = (size_t)(vend - value);
        if (vlen > TEKO_HTTP_MAX_HEADER_VALUE) return TEKO_HTTP_ERR_VALUE_TOO_LONG;

        if (*hcount >= TEKO_HTTP_MAX_HEADERS) return TEKO_HTTP_ERR_TOO_MANY_HEADERS;
        headers[*hcount].name  = name;
        headers[*hcount].value = value;
        (*hcount)++;

        /* Track Content-Length (duplicate = smuggling). */
        if (icase_eq(name, "Content-Length")) {
            if (*cl_seen) return TEKO_HTTP_ERR_SMUGGLING;
            if (!parse_content_length(value, cl)) return TEKO_HTTP_ERR_MALFORMED;
            *cl_seen = 1;
        }
        /* Track Transfer-Encoding: chunked. */
        if (icase_eq(name, "Transfer-Encoding")) {
            char *tv = value;
            while (*tv == ' ' || *tv == '\t') tv++;
            if (icase_eq(tv, "chunked")) *te_chunked = 1;
        }

        pos = crlf + 2;  /* skip CRLF */
    }
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Chunked decode (in-place into the mutable buf / fresh alloc for the
 * external API; used by both parse_request and parse_response).
 * ====================================================================== */

/*
 * Decode chunked body at buf[body_start..body_start+body_len-1] in-place.
 * Writes decoded bytes to buf[body_start..] (decoded body is always <= encoded).
 * Returns TEKO_HTTP_OK and sets *decoded_len on success.
 */
static int chunked_decode_inplace(char *buf, size_t body_start, size_t body_len,
                                   size_t *decoded_len) {
    size_t src = body_start;
    size_t end = body_start + body_len;
    size_t dst = body_start;
    size_t total = 0;

    while (src < end) {
        /* Read chunk-size hex string. */
        size_t hex_start = src;
        size_t hex_count = 0;
        while (src < end && buf[src] != '\r' && buf[src] != ';') {
            /* Validate hex digit. */
            char c = buf[src];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
                return TEKO_HTTP_ERR_BAD_CHUNK;
            hex_count++;
            if (hex_count > TEKO_HTTP_MAX_CHUNK_SIZE_DIGITS) return TEKO_HTTP_ERR_BAD_CHUNK;
            src++;
        }
        if (hex_count == 0) return TEKO_HTTP_ERR_BAD_CHUNK;

        /* Skip chunk extensions (if any). */
        while (src < end && buf[src] == ';') {
            while (src < end && buf[src] != '\r') src++;
        }

        /* Expect CRLF after size. */
        if (src + 1 >= end || buf[src] != '\r' || buf[src + 1] != '\n')
            return TEKO_HTTP_ERR_BAD_CHUNK;
        src += 2;

        /* Parse hex value. */
        size_t chunk_size = 0;
        for (size_t k = 0; k < hex_count; k++) {
            char c = buf[hex_start + k];
            size_t nibble;
            if      (c >= '0' && c <= '9') nibble = (size_t)(c - '0');
            else if (c >= 'a' && c <= 'f') nibble = (size_t)(c - 'a' + 10);
            else                           nibble = (size_t)(c - 'A' + 10);
            /* chunk_size = chunk_size * 16 + nibble; overflow-safe. */
            if (chunk_size > TEKO_HTTP_BODY_MAX / 16) return TEKO_HTTP_ERR_BODY_TOO_LARGE;
            chunk_size = chunk_size * 16 + nibble;
            if (chunk_size > TEKO_HTTP_BODY_MAX)      return TEKO_HTTP_ERR_BODY_TOO_LARGE;
        }

        if (chunk_size == 0) {
            /* Terminal chunk — expect CRLF (trailing headers not supported; just skip). */
            /* skip any trailer headers until the blank line */
            while (src + 1 < end) {
                if (buf[src] == '\r' && buf[src + 1] == '\n') { src += 2; break; }
                src++;
            }
            break;
        }

        /* Check total won't overflow. */
        size_t new_total;
        if (!safe_add_sz(total, chunk_size, &new_total)) return TEKO_HTTP_ERR_OVERFLOW;
        if (new_total > TEKO_HTTP_BODY_MAX)              return TEKO_HTTP_ERR_BODY_TOO_LARGE;

        /* Copy chunk data (src → dst; they may overlap if dst < src). */
        if (src + chunk_size > end) return TEKO_HTTP_ERR_TRUNCATED;
        if (dst != src) {
            /* Safe: dst <= src always (decoded is smaller-or-equal). */
            memmove(buf + dst, buf + src, chunk_size);
        }
        dst   += chunk_size;
        src   += chunk_size;
        total  = new_total;

        /* Expect CRLF after chunk data. */
        if (src + 1 >= end || buf[src] != '\r' || buf[src + 1] != '\n')
            return TEKO_HTTP_ERR_BAD_CHUNK;
        src += 2;
    }

    *decoded_len = total;
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: parse_request
 * ====================================================================== */

int teko_http_parse_request(char *buf, size_t len, TekoHttpRequest *req) {
    if (!buf || !req) return TEKO_HTTP_ERR_NULL_ARG;

    /* --- Request-line: METHOD SP path SP version CRLF --- */
    size_t crlf = find_crlf(buf, 0, len);
    if (crlf == (size_t)-1) return TEKO_HTTP_ERR_MALFORMED;

    /* Parse method (up to first SP). */
    size_t i = 0;
    while (i < crlf && buf[i] != ' ') i++;
    if (i == 0 || i == crlf) return TEKO_HTTP_ERR_MALFORMED;
    if (i > TEKO_HTTP_MAX_METHOD)  return TEKO_HTTP_ERR_METHOD_TOO_LONG;
    buf[i] = '\0';
    req->method = buf;

    /* Path (between first and second SP). */
    size_t path_start = i + 1;
    size_t j = path_start;
    while (j < crlf && buf[j] != ' ') j++;
    if (j == path_start || j == crlf) return TEKO_HTTP_ERR_MALFORMED;
    size_t path_len = j - path_start;
    if (path_len > TEKO_HTTP_MAX_PATH) return TEKO_HTTP_ERR_PATH_TOO_LONG;
    buf[j] = '\0';
    req->path = buf + path_start;

    /* Version (rest of line). */
    size_t ver_start = j + 1;
    size_t ver_len   = crlf - ver_start;
    if (ver_len == 0 || ver_len > TEKO_HTTP_MAX_VERSION) return TEKO_HTTP_ERR_MALFORMED;
    buf[crlf] = '\0';
    req->version = buf + ver_start;

    /* --- Headers --- */
    size_t hdr_start = crlf + 2;
    /* Find blank line (\r\n\r\n).  k+3 is the last index accessed, so k <= len-4. */
    size_t blank = (size_t)-1;
    if (len >= 4) {
        for (size_t k = hdr_start; k <= len - 4; k++) {
            if (buf[k] == '\r' && buf[k+1] == '\n' &&
                buf[k+2] == '\r' && buf[k+3] == '\n') {
                blank = k;
                break;
            }
        }
    }
    if (blank == (size_t)-1) return TEKO_HTTP_ERR_MALFORMED;

    int    cl_seen    = 0;
    size_t cl         = 0;
    int    te_chunked = 0;
    int rc = parse_headers(buf, hdr_start, blank, req->headers, &req->header_count,
                           &cl_seen, &cl, &te_chunked);
    if (rc != TEKO_HTTP_OK) return rc;

    /* --- Body --- */
    size_t body_start = blank + 4;  /* skip \r\n\r\n */

    if (te_chunked) {
        size_t encoded_body_len;
        if (body_start > len) return TEKO_HTTP_ERR_TRUNCATED;
        encoded_body_len = len - body_start;
        size_t decoded_len = 0;
        rc = chunked_decode_inplace(buf, body_start, encoded_body_len, &decoded_len);
        if (rc != TEKO_HTTP_OK) return rc;
        req->body     = buf + body_start;
        req->body_len = decoded_len;
    } else if (cl_seen) {
        if (body_start > len || len - body_start < cl) return TEKO_HTTP_ERR_TRUNCATED;
        req->body     = buf + body_start;
        req->body_len = cl;
    } else {
        req->body     = NULL;
        req->body_len = 0;
    }

    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: parse_response
 * ====================================================================== */

int teko_http_parse_response(char *buf, size_t len, TekoHttpResponse *resp) {
    if (!buf || !resp) return TEKO_HTTP_ERR_NULL_ARG;

    /* --- Status-line: version SP 3digits SP reason CRLF --- */
    size_t crlf = find_crlf(buf, 0, len);
    if (crlf == (size_t)-1) return TEKO_HTTP_ERR_MALFORMED;

    /* Version token. */
    size_t i = 0;
    while (i < crlf && buf[i] != ' ') i++;
    if (i == 0 || i == crlf) return TEKO_HTTP_ERR_MALFORMED;
    if (i > TEKO_HTTP_MAX_VERSION) return TEKO_HTTP_ERR_MALFORMED;
    buf[i] = '\0';
    resp->version = buf;

    /* Status code: exactly 3 decimal digits. */
    size_t sc_start = i + 1;
    if (sc_start + 3 > crlf) return TEKO_HTTP_ERR_MALFORMED;
    char d0 = buf[sc_start], d1 = buf[sc_start + 1], d2 = buf[sc_start + 2];
    if (d0 < '1' || d0 > '9' || d1 < '0' || d1 > '9' || d2 < '0' || d2 > '9')
        return TEKO_HTTP_ERR_MALFORMED;
    if (buf[sc_start + 3] != ' ' && buf[sc_start + 3] != '\r')
        return TEKO_HTTP_ERR_MALFORMED;
    resp->status_code = (d0 - '0') * 100 + (d1 - '0') * 10 + (d2 - '0');

    /* Reason phrase (optional). */
    size_t rp_start = sc_start + 3;
    if (rp_start < crlf && buf[rp_start] == ' ') rp_start++;
    buf[crlf] = '\0';
    resp->reason_phrase = buf + rp_start;

    /* --- Headers --- */
    size_t hdr_start = crlf + 2;
    size_t blank = (size_t)-1;
    if (len >= 4) {
        for (size_t k = hdr_start; k <= len - 4; k++) {
            if (buf[k] == '\r' && buf[k+1] == '\n' &&
                buf[k+2] == '\r' && buf[k+3] == '\n') {
                blank = k;
                break;
            }
        }
    }
    if (blank == (size_t)-1) return TEKO_HTTP_ERR_MALFORMED;

    int    cl_seen    = 0;
    size_t cl         = 0;
    int    te_chunked = 0;
    int rc = parse_headers(buf, hdr_start, blank, resp->headers, &resp->header_count,
                           &cl_seen, &cl, &te_chunked);
    if (rc != TEKO_HTTP_OK) return rc;

    /* --- Body --- */
    size_t body_start = blank + 4;

    if (te_chunked) {
        size_t encoded_body_len;
        if (body_start > len) return TEKO_HTTP_ERR_TRUNCATED;
        encoded_body_len = len - body_start;
        size_t decoded_len = 0;
        rc = chunked_decode_inplace(buf, body_start, encoded_body_len, &decoded_len);
        if (rc != TEKO_HTTP_OK) return rc;
        resp->body     = buf + body_start;
        resp->body_len = decoded_len;
    } else if (cl_seen) {
        if (body_start > len || len - body_start < cl) return TEKO_HTTP_ERR_TRUNCATED;
        resp->body     = buf + body_start;
        resp->body_len = cl;
    } else {
        resp->body     = NULL;
        resp->body_len = 0;
    }

    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: chunked_encode
 * ====================================================================== */

int teko_http_chunked_encode(
        const char *src,
        size_t      src_len,
        char      **out_buf,
        size_t     *out_len)
{
    if (!out_buf || !out_len) return TEKO_HTTP_ERR_NULL_ARG;
    *out_buf = NULL;
    *out_len = 0;

    if (src_len > 0 && !src) return TEKO_HTTP_ERR_NULL_ARG;
    if (src_len > TEKO_HTTP_BODY_MAX) return TEKO_HTTP_ERR_BODY_TOO_LARGE;

    /* Compute hex string length for src_len. */
    char hex[20];
    size_t hv = src_len;
    int hi = (int)sizeof(hex);
    hex[--hi] = '\0';
    if (hv == 0) { hex[--hi] = '0'; }
    else {
        while (hv > 0) {
            hex[--hi] = "0123456789abcdef"[hv & 0xf];
            hv >>= 4;
        }
    }
    size_t hexlen = sizeof(hex) - (size_t)hi - 1;

    /*
     * Layout: <hexlen_bytes>\r\n<src_len_bytes>\r\n0\r\n\r\n
     * Total = hexlen + 2 + src_len + 2 + 1 + 2 + 2 = hexlen + src_len + 9
     * All overflow-checked.
     */
    size_t total;
    if (!safe_add_sz(hexlen, src_len, &total))      return TEKO_HTTP_ERR_OVERFLOW;
    if (!safe_add_sz(total,  9,       &total))      return TEKO_HTTP_ERR_OVERFLOW;
    if (total > TEKO_HTTP_BODY_MAX + TEKO_HTTP_CHUNKED_OVERHEAD) return TEKO_HTTP_ERR_OVERFLOW;

    char *out = (char *)malloc(total + 1);
    if (!out) return TEKO_HTTP_ERR_ALLOC;

    size_t pos = 0;
    memcpy(out + pos, hex + hi, hexlen);  pos += hexlen;
    out[pos++] = '\r'; out[pos++] = '\n';
    if (src_len > 0) { memcpy(out + pos, src, src_len); pos += src_len; }
    out[pos++] = '\r'; out[pos++] = '\n';
    if (src_len > 0) {
        out[pos++] = '0';
        out[pos++] = '\r'; out[pos++] = '\n';
        out[pos++] = '\r'; out[pos++] = '\n';
    }
    out[pos]   = '\0';

    *out_buf = out;
    *out_len = pos;
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: chunked_decode
 * ====================================================================== */

int teko_http_chunked_decode(
        const char *src,
        size_t      src_len,
        char      **out_buf,
        size_t     *out_len)
{
    if (!out_buf || !out_len) return TEKO_HTTP_ERR_NULL_ARG;
    *out_buf = NULL;
    *out_len = 0;
    if (src_len > 0 && !src) return TEKO_HTTP_ERR_NULL_ARG;
    if (src_len == 0) {
        char *empty = (char *)malloc(1);
        if (!empty) return TEKO_HTTP_ERR_ALLOC;
        empty[0] = '\0';
        *out_buf = empty;
        *out_len = 0;
        return TEKO_HTTP_OK;
    }

    /* Allocate a mutable copy for in-place decode. */
    char *work = (char *)malloc(src_len + 1);
    if (!work) return TEKO_HTTP_ERR_ALLOC;
    memcpy(work, src, src_len);
    work[src_len] = '\0';

    size_t decoded_len = 0;
    int rc = chunked_decode_inplace(work, 0, src_len, &decoded_len);
    if (rc != TEKO_HTTP_OK) { free(work); return rc; }

    /* Return a right-sized copy. */
    char *out = (char *)malloc(decoded_len + 1);
    if (!out) { free(work); return TEKO_HTTP_ERR_ALLOC; }
    memcpy(out, work, decoded_len);
    out[decoded_len] = '\0';
    free(work);

    *out_buf = out;
    *out_len = decoded_len;
    return TEKO_HTTP_OK;
}

/* =========================================================================
 * Public: find_header
 * ====================================================================== */

const TekoHttpHeader *teko_http_find_header(
        const TekoHttpHeader *headers,
        size_t                count,
        const char           *name)
{
    if (!headers || !name) return NULL;
    for (size_t i = 0; i < count; i++) {
        if (headers[i].name && icase_eq(headers[i].name, name)) return &headers[i];
    }
    return NULL;
}
