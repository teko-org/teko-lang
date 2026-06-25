// src/parser/parse_lit.h   (namespace 'teko::parser')
//
// Literal decoders, the C23 mirror of parser/parse_lit.tks. RELOCATED from the lexer
// (legislator decision): these belong to the parser namespace in Teko, so their C pair
// now lives here. The parser (parse_expr / parse_pattern) includes this header.
#ifndef TK_PARSER_PARSE_LIT_H
#define TK_PARSER_PARSE_LIT_H

#include "../text/text.h"   // tk_str, tk_byte

#include <stdint.h>         // int64_t
#include <stdbool.h>        // bool

// A Number token's text (decimal digits with `_` separators) → a 128-bit value
// (N1). The literal node carries the raw integer; `.type` decides the real width.
// (C bootstrap carrier is __int128; the canonical Teko spelling uses a native int.)
__int128 tk_lit_int(tk_str text);
// A FLOAT Number token's text (`3.14`, `1.5e3`) → double (default f64; an annotation
// picks f16/f32). `_` separators are stripped before the host strtod parse.
double tk_lit_float(tk_str text);
// Predicate: does this Number token's text denote a float? (contains `.`/`e`/`E`).
bool tk_lit_is_float(tk_str text);
// A Byte token's text is the already-decoded octet (the lexer resolved it).
tk_byte tk_lit_byte(tk_str text);

#endif // TK_PARSER_PARSE_LIT_H
