// Phase 16 (Casting / Type Conversions & Parsing) — culture-invariant conversion runtime.
// See teko_convert.h. Hand-rolled, freestanding-safe (no snprintf/strtoll/strtod/setlocale):
// the only libc calls are malloc/strlen/memcpy, all provided by the wasm32 reactor shim.

#include "teko_convert.h"
#include <stdlib.h>  // malloc
#include <string.h>  // strlen, memcpy

// --- to-string -------------------------------------------------------------------

char* teko_convert_i64_to_string(long long v) {
    // Build digits into a fixed buffer (INT64_MIN = -9223372036854775808 = 19 digits + sign + NUL).
    char tmp[24];
    int i = (int)sizeof(tmp);
    tmp[--i] = '\0';
    int neg = (v < 0);
    // Accumulate on the NEGATIVE side so INT64_MIN does not overflow (|INT64_MIN| > INT64_MAX).
    long long n = v;
    if (!neg) n = -n;          // now n <= 0 for all inputs
    do {
        int digit = (int)(-(n % 10));   // 0..9
        tmp[--i] = (char)('0' + digit);
        n /= 10;
    } while (n != 0);
    if (neg) tmp[--i] = '-';
    size_t len = sizeof(tmp) - (size_t)i;   // includes the NUL
    char* out = (char*)malloc(len);
    if (!out) return NULL;
    memcpy(out, tmp + i, len);
    return out;
}

char* teko_convert_bool_to_string(int v) {
    const char* s = v ? "true" : "false";
    size_t len = strlen(s) + 1;
    char* out = (char*)malloc(len);
    if (!out) return NULL;
    memcpy(out, s, len);
    return out;
}

char* teko_convert_str_concat(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    size_t la = strlen(a), lb = strlen(b);
    char* out = (char*)malloc(la + lb + 1);
    if (!out) return NULL;
    memcpy(out, a, la);
    memcpy(out + la, b, lb);
    out[la + lb] = '\0';
    return out;
}

// --- parse (checked) -------------------------------------------------------------

static int is_ws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

int teko_convert_parse_i64(const char* s, long long* out) {
    if (!s) return 0;
    const char* p = s;
    while (is_ws(*p)) p++;
    int neg = 0;
    if (*p == '+' || *p == '-') { neg = (*p == '-'); p++; }
    if (*p < '0' || *p > '9') return 0;          // need at least one digit
    // Accumulate on the negative side to represent INT64_MIN without overflow.
    long long acc = 0;
    const long long LIM = (-9223372036854775807LL - 1);  // INT64_MIN
    while (*p >= '0' && *p <= '9') {
        int d = *p - '0';
        if (acc < LIM / 10) return 0;                     // would overflow on *10
        acc *= 10;
        if (acc < LIM + d) return 0;                      // would overflow on -d
        acc -= d;
        p++;
    }
    while (is_ws(*p)) p++;
    if (*p != '\0') return 0;                             // trailing junk
    long long val;
    if (neg) {
        val = acc;
    } else {
        if (acc == LIM) return 0;                         // +9223372036854775808 not representable
        val = -acc;
    }
    if (out) *out = val;
    return 1;
}

int teko_convert_parse_bool(const char* s, int* out) {
    if (!s) return 0;
    const char* p = s;
    while (is_ws(*p)) p++;
    int val;
    if (p[0]=='t' && p[1]=='r' && p[2]=='u' && p[3]=='e') { val = 1; p += 4; }
    else if (p[0]=='f' && p[1]=='a' && p[2]=='l' && p[3]=='s' && p[4]=='e') { val = 0; p += 5; }
    else return 0;
    while (is_ws(*p)) p++;
    if (*p != '\0') return 0;
    if (out) *out = val;
    return 1;
}
