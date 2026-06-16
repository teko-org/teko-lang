#ifndef TEKO_CONVERT_H
#define TEKO_CONVERT_H

// Phase 16 (Casting / Type Conversions & Parsing) — the culture-invariant conversion runtime
// (the single source of truth, KAT-tested in the Unity suite, compiled to the WASM reactor and
// linked via teko_rt for native). Every routine here is PORTABLE, hand-rolled C with NO libc
// number/locale calls (no snprintf/strtoll/strtod, no setlocale) — so the representation is
// identical on every machine/region (`.`-decimal, no digit grouping, canonical grammar) and the
// reactor stays freestanding (the wasm32 libc shim provides only malloc/memcpy/strlen). This is
// DELIBERATELY distinct from Phase 14's `time.format_local`, which is OS-locale/DST-aware.
//
// Strings are fresh heap allocations the caller owns (free), matching teko_time.c. The parse
// routines are CHECKED: they return 1 on a fully-valid canonical input and 0 otherwise (no silent
// truncation), writing the value through the out-pointer only on success.

// --- to-string (culture-invariant default) ---------------------------------------
// Signed 64-bit integer -> decimal string (INT64_MIN-safe, no grouping, leading '-' for negatives).
char* teko_convert_i64_to_string(long long v);
// Boolean (0 -> "false", any non-zero -> "true").
char* teko_convert_bool_to_string(int v);
// Concatenate two NUL-terminated strings into a fresh buffer. NULL is treated as "".
char* teko_convert_str_concat(const char* a, const char* b);

// Phase 17.C — shortest-round-trip f64 -> culture-invariant `.`-decimal string (Ryu).
// Always >= 1 fractional digit (1.0, 100.0); specials NaN/Infinity/-Infinity/0.0/-0.0;
// `e`-notation only outside -4 <= e10 < 21. Fresh malloc'd buffer (caller frees), NULL
// on OOM. See teko_convert_f64.c for the full algorithm + renderer-policy notice. The
// `convert.float_to_str` surface (id 50) is wired in 17.D — this is the C core only.
char* teko_convert_f64_to_string(double v);

// Phase 17.E — CHECKED string -> f64 parse (the inverse of teko_convert_f64_to_string).
// Freestanding (no strtod/math.h/setlocale/__int128) and CORRECTLY ROUNDED (round-to-nearest-even)
// so parse(format(d)) == d bit-for-bit. Accepted grammar (culture-invariant): optional surrounding
// ASCII whitespace, optional '+'/'-', decimal digits with an optional single '.', optional exponent
// e/E[+/-]digits. Rejects empty/junk/lone '.'/lone 'e'/specials (NaN/Infinity), AND overflow to
// ±Inf (fail-loud); underflow to a subnormal/0 is fine. Returns 1 + writes *out on success, else 0
// (and does not write *out). The `convert.parse_float` surface (id 54) is wired in 17.E.
int teko_convert_parse_f64(const char* s, double* out);

// --- explicit format (developer-supplied spec; deviates from the default) ---------
// Phase 16.E. These are the EXPLICIT formats a developer opts into — distinct from the universal
// culture-invariant default (plain `.`-decimal, no grouping). Still locale-independent.
// Integer in an arbitrary radix 2..36 (lowercase digits, '-' for negatives, INT64_MIN-safe).
char* teko_convert_i64_to_radix(long long v, int radix);
// Decimal, zero-padded to a minimum total width (the '-' sign counts toward the width).
char* teko_convert_i64_pad(long long v, int width);
// Decimal with `sep` inserted every 3 digits from the right (e.g. ',' -> "1,000,000").
char* teko_convert_i64_grouped(long long v, char sep);

// --- parse (checked; 1 = valid, 0 = malformed/overflow) ---------------------------
// Canonical signed decimal integer: optional surrounding ASCII whitespace, optional leading
// '+'/'-', one or more digits, no digit grouping, nothing else. Overflow of int64 fails.
int teko_convert_parse_i64(const char* s, long long* out);
// Canonical boolean: "true"/"false" (optionally surrounded by ASCII whitespace). Strict.
int teko_convert_parse_bool(const char* s, int* out);

#endif // TEKO_CONVERT_H
