// src/parser/parse_lit.c   (namespace 'teko::parser')
//
// Literal decoders, the C23 mirror of parser/parse_lit.tks. RELOCATED from the lexer
// (the bodies were formerly in src/lexer/lexer.c) — these are parser-namespace fns.
#include "parse_lit.h"

#include <stdlib.h>   // strtod
#include <string.h>   // memcpy

// a Number token's text (decimal digits with `_` separators) → a 128-bit value
// (N1 — TEKO_CORRECTION_PLAN §5). The literal node carries the raw integer; its
// resolved `.type` (set by the typer) decides the actual width/signedness.
__int128 tk_lit_int(tk_str text) {
    // hex `0x…` / binary `0b…` (B.28) decode by radix; `_` separators ignored.
    if (text.len >= 2 && text.ptr[0] == '0' && (text.ptr[1] == 'x' || text.ptr[1] == 'X')) {
        __int128 acc = 0;
        for (size_t i = 2; i < text.len; i += 1) {
            tk_byte c = text.ptr[i];
            if (c == '_') continue;
            int d = (c >= '0' && c <= '9') ? c - '0'
                  : (c >= 'a' && c <= 'f') ? c - 'a' + 10
                  : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
            if (d < 0) break;
            acc = acc * 16 + (__int128)d;
        }
        return acc;
    }
    if (text.len >= 2 && text.ptr[0] == '0' && (text.ptr[1] == 'b' || text.ptr[1] == 'B')) {
        __int128 acc = 0;
        for (size_t i = 2; i < text.len; i += 1) {
            tk_byte c = text.ptr[i];
            if (c == '_') continue;
            if (c != '0' && c != '1') break;
            acc = acc * 2 + (__int128)(c - '0');
        }
        return acc;
    }
    __int128 acc = 0;
    size_t i = 0;
    for (;;) {
        if (i >= text.len) break;
        tk_byte c = text.ptr[i];
        if (c != '_') {
            acc = acc * 10 + (__int128)c - (__int128)'0';
        }
        i++;
    }
    return acc;
}

// a FLOAT Number token's text (`3.14`, `1.5e3`) → double (default f64; an annotation
// picks f16/f32 later). `_` separators are stripped before the host strtod parse.
// Contained: no token suffixes (`1.5f` is NOT a thing — N1).
double tk_lit_float(tk_str text) {
    // copy out the digits (dropping `_`) into a NUL-terminated host buffer for strtod.
    char buf[64];
    size_t j = 0;
    for (size_t i = 0; i < text.len && j + 1 < sizeof buf; i += 1) {
        tk_byte c = text.ptr[i];
        if (c == '_') continue;
        buf[j++] = (char)c;
    }
    buf[j] = '\0';
    return strtod(buf, NULL);
}

// predicate: does this Number token's text denote a FLOAT? (contains `.` or `e`/`E`).
// The parser/typer uses this to choose the integer vs float carrier (N1).
bool tk_lit_is_float(tk_str text) {
    // a hex/binary literal is ALWAYS an integer — its digits may include 'e'/'E' (hex E)
    // which must NOT be mistaken for a float exponent.
    if (text.len >= 2 && text.ptr[0] == '0' &&
        (text.ptr[1] == 'x' || text.ptr[1] == 'X' || text.ptr[1] == 'b' || text.ptr[1] == 'B')) return false;
    for (size_t i = 0; i < text.len; i += 1) {
        tk_byte c = text.ptr[i];
        if (c == '.' || c == 'e' || c == 'E') return true;
    }
    return false;
}

// a Byte token's text is the already-decoded octet (the lexer resolved it).
tk_byte tk_lit_byte(tk_str text) {
    return text.ptr[0];
}
