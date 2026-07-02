// src/lexer/lexer.c — teko::lexer scanner, the C23 mirror of lexer.tks.
//
// Faithful transcription of the Teko scanner: the lex loop, keyword/symbol/number/
// string/byte/comment readers, plus the literal decoders (parse_lit.tks). Same token
// productions and edge cases as the Teko source (maximal munch — B.23).
#include "lexer.h"

#include <stdint.h>
#include <stdlib.h>   // malloc, abort
#include <string.h>   // strlen
#include <stdio.h>    // sprintf

// a private growable byte buffer for decoded string literals — teko::list over
// byte. Local to the lexer (the DAG is lexer → text → core; we must NOT depend on
// the downstream emit module that also defines a byte list).
TK_LIST(tk_byte, tk_lex_bytes);

// --- a read result: the token + where scanning stopped (lexer.tks `Scan`) ---
typedef struct { tk_token token; size_t next; } tk_scan;

// `Scan | error` and `u64 | error` results (TK_RESULT mirrors `T | error`).
TK_RESULT(tk_scan, tk_scan_result);
TK_RESULT(size_t, tk_pos_result);

static tk_scan_result scan_ok(tk_scan s) {
    return (tk_scan_result){ .ok = true, .as.value = s };
}
static tk_scan_result scan_err(const char *msg) {
    return (tk_scan_result){ .ok = false, .as.error = tk_error_make(msg) };
}

// --- byte access (lexer.tks `at`): the byte at p, or 0 if out of bounds ---
static tk_byte at(tk_str source, size_t p) {
    if (p >= source.len) return 0;
    return source.ptr[p];
}

// --- source LOCATION: the 1-based (line, col) of byte `pos` (for token stamping +
//     file:line:col diagnostics — M.3 honest about WHERE). O(pos) scan; the bootstrap
//     files are small (M.5 — a line table is a later optimization, not needed now). ---
static void compute_loc(tk_str source, size_t pos, uint32_t *line, uint32_t *col) {
    uint32_t l = 1, c = 1;
    size_t limit = pos < source.len ? pos : source.len;
    for (size_t i = 0; i < limit; i += 1) {
        if (source.ptr[i] == '\n') { l += 1; c = 1; } else { c += 1; }
    }
    *line = l; *col = c;
}
// stamp a token with an ALREADY-COMPUTED 1-based (line, col). tk_tokenize threads a location
// cursor that advances MONOTONICALLY with the token-start position, so each source byte is
// crossed exactly once across the whole file — O(1) amortized per token (a line table without
// the table). The old per-token compute_loc rescan was O(pos), i.e. O(N²) over a file.
static tk_token stamp_loc(tk_token tok, uint32_t line, uint32_t col) {
    tok.line = line; tok.col = col;
    return tok;
}
// a LOCATED lexer error: "line:col: msg" at byte `pos` (the file is prepended later by the
// driver/assemble — same shape as parse errors, so lexer errors carry file:line:col too — M.3).
static tk_scan_result scan_err_at(tk_str source, size_t pos, const char *msg) {
    uint32_t line, col; compute_loc(source, pos, &line, &col);
    char *buf = tk_alloc(strlen(msg) + 32); if (!buf) abort();
    sprintf(buf, "%u:%u: %s", line, col, msg);
    return (tk_scan_result){ .ok = false, .as.error = tk_error_make(buf) };
}

// --- predicates (pure, over a single byte) ---
static bool is_digit(tk_byte c) { return c >= '0' && c <= '9'; }
static bool is_hex_digit(tk_byte c) { return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static bool is_alpha(tk_byte c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
static bool is_ident_continue(tk_byte c) {
    return is_alpha(c) || is_digit(c) || c == '_';
}

// --- the common Scan builder (lexer.tks `sym`): a token spanning n bytes ---
static tk_scan sym(tk_str source, size_t pos, size_t n, tk_token_kind kind) {
    return (tk_scan){
        .token = (tk_token){ .kind = kind, .text = tk_str_slice(source, pos, pos + n) },
        .next  = pos + n,
    };
}

// --- whitespace & comments (pure: take pos, return the new pos) ---

// skip spaces/tabs/CR — NOT newlines (a newline is a significant token, B.26).
static size_t skip_spaces(tk_str source, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (p >= source.len) break;
        tk_byte c = source.ptr[p];
        if (c != ' ' && c != '\t' && c != '\r') break;
        p++;
    }
    return p;
}

// skip a `//` line comment: advance to the newline (or end), leaving it for the loop.
static size_t skip_line(tk_str source, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (p >= source.len) break;
        if (source.ptr[p] == '\n') break;
        p++;
    }
    return p;
}

// skip a block comment `/* … */` (not nested); returns the pos after the `*/`.
static tk_pos_result skip_block_comment(tk_str source, size_t pos) {
    size_t p = pos + 2;                       // past `/*`
    for (;;) {
        if (p + 1 >= source.len) {
            return (tk_pos_result){ .ok = false,
                .as.error = tk_error_make("unterminated block comment") };
        }
        if (source.ptr[p] == '*' && source.ptr[p + 1] == '/') {
            return (tk_pos_result){ .ok = true, .as.value = p + 2 };
        }
        p++;
    }
}

// read a DOC comment `/** … */` → a Doc token spanning the whole comment.
static tk_scan_result read_doc_comment(tk_str source, size_t pos) {
    size_t p = pos + 3;                       // past `/**`
    for (;;) {
        if (p + 1 >= source.len) return scan_err("unterminated doc comment");
        if (source.ptr[p] == '*' && source.ptr[p + 1] == '/') {
            size_t end = p + 2;
            return scan_ok((tk_scan){
                .token = (tk_token){ .kind = TK_TOKEN_DOC,
                                     .text = tk_str_slice(source, pos, end) },
                .next  = end,
            });
        }
        p++;
    }
}

// --- numbers & identifiers ---

// a run of digits, allowing `_` as a separator BETWEEN digits (B.28): `1_000`.
static size_t scan_digits(tk_str source, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (p >= source.len) break;
        tk_byte c = source.ptr[p];
        if (is_digit(c)) { p++; continue; }
        if (c == '_') {
            if (p + 1 < source.len && is_digit(at(source, p + 1))) { p++; continue; }
        }
        break;
    }
    return p;
}

// a Number token — integer (`123`, `1_000`) OR float (`3.14`, `1.5e3`, `1e9`) (N1 —
// TEKO_CORRECTION_PLAN §5). The token stays TK_TOKEN_NUMBER carrying the raw text;
// the int-vs-float decision is made by the literal decoders (tk_lit_is_float). No
// float SUFFIXES (`1.5f`): the default is f64, an annotation picks f16/f32.
static tk_scan read_number(tk_str source, size_t pos) {
    // hex `0x…` / binary `0b…` integer literals (B.28; no fraction/exponent). `_` may
    // separate digits. The raw text (incl the prefix) rides the token; tk_lit_int decodes.
    if (at(source, pos) == '0' && (at(source, pos + 1) == 'x' || at(source, pos + 1) == 'X')) {
        size_t h = pos + 2;
        while (h < source.len && (is_hex_digit(source.ptr[h]) ||
               (source.ptr[h] == '_' && h + 1 < source.len && is_hex_digit(at(source, h + 1))))) h++;
        return (tk_scan){ .token = (tk_token){ .kind = TK_TOKEN_NUMBER, .text = tk_str_slice(source, pos, h) }, .next = h };
    }
    if (at(source, pos) == '0' && (at(source, pos + 1) == 'b' || at(source, pos + 1) == 'B')) {
        size_t bp = pos + 2;
        while (bp < source.len && (source.ptr[bp] == '0' || source.ptr[bp] == '1' ||
               (source.ptr[bp] == '_' && bp + 1 < source.len && (at(source, bp + 1) == '0' || at(source, bp + 1) == '1')))) bp++;
        return (tk_scan){ .token = (tk_token){ .kind = TK_TOKEN_NUMBER, .text = tk_str_slice(source, pos, bp) }, .next = bp };
    }
    size_t p = scan_digits(source, pos);          // integer part

    // fractional part: a `.` FOLLOWED BY a digit (so `1.method` / `1..=` stay ints).
    if (at(source, p) == '.' && is_digit(at(source, p + 1))) {
        p = scan_digits(source, p + 1);
    }

    // exponent: `[eE][+-]?<digits>` — only consume `e` if a valid exponent follows
    // (so a stray `e` is left for the identifier/operator path).
    if (at(source, p) == 'e' || at(source, p) == 'E') {
        size_t q = p + 1;
        if (at(source, q) == '+' || at(source, q) == '-') q += 1;
        if (is_digit(at(source, q))) {
            p = scan_digits(source, q);
        }
    }

    return (tk_scan){
        .token = (tk_token){ .kind = TK_TOKEN_NUMBER, .text = tk_str_slice(source, pos, p) },
        .next  = p,
    };
}

static tk_scan read_ident(tk_str source, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (p >= source.len) break;
        if (!is_ident_continue(source.ptr[p])) break;
        p++;
    }
    return (tk_scan){
        .token = (tk_token){ .kind = TK_TOKEN_IDENT, .text = tk_str_slice(source, pos, p) },
        .next  = p,
    };
}

// a run of `_` then a letter/digit is ONE identifier (`_foo`, `__bar`); a `_` not so
// followed is the wildcard token (emitted one at a time — the parser rejects a run).
static tk_scan read_underscore(tk_str source, size_t pos) {
    size_t p = pos;
    for (;;) {
        if (p >= source.len) break;
        if (source.ptr[p] != '_') break;
        p++;
    }
    if (p < source.len) {
        if (is_alpha(source.ptr[p]) || is_digit(source.ptr[p])) {
            return read_ident(source, pos);   // reads from the first `_`
        }
    }
    return sym(source, pos, 1, TK_TOKEN_UNDERSCORE);
}

// --- keywords (an ident whose text matches the table — B.19) ---

static bool tk_str_eq_lit(tk_str text, const char *lit) {
    size_t i = 0;
    for (; i < text.len; i += 1) {
        if (lit[i] == '\0') return false;        // lit shorter than text
        if (text.ptr[i] != (tk_byte)lit[i]) return false;
    }
    return lit[i] == '\0';                        // both ended together
}

static tk_token_kind keyword_kind(tk_str text) {
    if (tk_str_eq_lit(text, "fn"))       return TK_TOKEN_FN;
    if (tk_str_eq_lit(text, "type"))     return TK_TOKEN_TYPE;
    if (tk_str_eq_lit(text, "struct"))   return TK_TOKEN_STRUCT;
    if (tk_str_eq_lit(text, "enum"))     return TK_TOKEN_ENUM;
    if (tk_str_eq_lit(text, "flags"))    return TK_TOKEN_FLAGS;
    if (tk_str_eq_lit(text, "variant"))  return TK_TOKEN_VARIANT;
    if (tk_str_eq_lit(text, "let"))      return TK_TOKEN_LET;
    if (tk_str_eq_lit(text, "mut"))      return TK_TOKEN_MUT;
    if (tk_str_eq_lit(text, "const"))    return TK_TOKEN_CONST;
    if (tk_str_eq_lit(text, "if"))       return TK_TOKEN_IF;
    if (tk_str_eq_lit(text, "else"))     return TK_TOKEN_ELSE;
    if (tk_str_eq_lit(text, "loop"))     return TK_TOKEN_LOOP;
    if (tk_str_eq_lit(text, "break"))    return TK_TOKEN_BREAK;
    if (tk_str_eq_lit(text, "continue")) return TK_TOKEN_CONTINUE;
    if (tk_str_eq_lit(text, "return"))   return TK_TOKEN_RETURN;
    if (tk_str_eq_lit(text, "defer"))    return TK_TOKEN_DEFER;   // scoped cleanup block (C7.18)
    if (tk_str_eq_lit(text, "match"))    return TK_TOKEN_MATCH;
    if (tk_str_eq_lit(text, "when"))     return TK_TOKEN_WHEN;
    if (tk_str_eq_lit(text, "in"))       return TK_TOKEN_IN;    // the membership operator (x in [ … ]) — Phase 2
    if (tk_str_eq_lit(text, "as"))       return TK_TOKEN_AS;
    if (tk_str_eq_lit(text, "to"))       return TK_TOKEN_TO;   // the cast operator (x to T) — F1/E7
    if (tk_str_eq_lit(text, "use"))      return TK_TOKEN_USE;
    if (tk_str_eq_lit(text, "pub"))      return TK_TOKEN_PUB;   // public within the project (B.9)
    if (tk_str_eq_lit(text, "exp"))      return TK_TOKEN_EXP;
    if (tk_str_eq_lit(text, "extern"))   return TK_TOKEN_EXTERN;   // foreign-function declarator (C7.1a)
    if (tk_str_eq_lit(text, "true"))     return TK_TOKEN_TRUE;   // bool literal (LEGISLATION §75)
    if (tk_str_eq_lit(text, "false"))    return TK_TOKEN_FALSE;  // bool literal (LEGISLATION §75)
    if (tk_str_eq_lit(text, "null"))     return TK_TOKEN_NULL;   // null literal (REBOOT_PLAN §202)
    if (tk_str_eq_lit(text, "class"))    return TK_TOKEN_CLASS;      // W10b.CLASS (2026-07-01)
    if (tk_str_eq_lit(text, "abstract")) return TK_TOKEN_ABSTRACT;
    if (tk_str_eq_lit(text, "virtual"))  return TK_TOKEN_VIRTUAL;
    if (tk_str_eq_lit(text, "override")) return TK_TOKEN_OVERRIDE;
    if (tk_str_eq_lit(text, "intern"))   return TK_TOKEN_INTERN;
    if (tk_str_eq_lit(text, "interface")) return TK_TOKEN_INTERFACE;   // (W10b.IF)
    // `params` is deliberately NOT here — it's a contextual keyword like `from` (see token.h).
    // `self`/`base` are likewise deliberately NOT here — see token.h's W10b.CLASS note.
    return TK_TOKEN_IDENT;
}

static tk_scan keyword_or_ident(tk_str source, size_t pos) {
    tk_scan s = read_ident(source, pos);
    tk_token_kind k = keyword_kind(s.token.text);
    return (tk_scan){
        .token = (tk_token){ .kind = k, .text = s.token.text },
        .next  = s.next,
    };
}

// --- escapes & fresh-byte → str ---

// a decoded escape: the byte + the pos after it (lexer.tks `EscByte`).
typedef struct { tk_byte value; size_t next; } tk_esc_byte;
typedef struct { tk_byte value; size_t next; } tk_byte_val;
TK_RESULT(tk_esc_byte, tk_esc_result);
TK_RESULT(tk_byte_val, tk_byteval_result);

// decode `\` + one byte → the byte; the set is the structural minimum (it can grow).
static tk_esc_result escape_byte(tk_str source, size_t pos) {
    size_t after = pos + 1;
    if (after >= source.len) {
        return (tk_esc_result){ .ok = false,
            .as.error = tk_error_make("unterminated escape") };
    }
    tk_byte v;
    switch (source.ptr[after]) {
        case 'n':  v = 0x0A; break;
        case 't':  v = 0x09; break;
        case 'r':  v = 0x0D; break;
        case '\\': v = 0x5C; break;
        case '"':  v = 0x22; break;
        case '\'': v = 0x27; break;
        case '0':  v = 0x00; break;
        default:
            return (tk_esc_result){ .ok = false,
                .as.error = tk_error_make("unknown escape") };
    }
    return (tk_esc_result){ .ok = true,
        .as.value = (tk_esc_byte){ .value = v, .next = after + 1 } };
}

// one byte of a byte literal: a raw byte, or an escape.
static tk_byteval_result byte_value(tk_str source, size_t pos) {
    if (source.ptr[pos] == '\\') {
        tk_esc_result e = escape_byte(source, pos);
        if (!e.ok) return (tk_byteval_result){ .ok = false, .as.error = e.as.error };
        return (tk_byteval_result){ .ok = true,
            .as.value = (tk_byte_val){ .value = e.as.value.value, .next = e.as.value.next } };
    }
    return (tk_byteval_result){ .ok = true,
        .as.value = (tk_byte_val){ .value = source.ptr[pos], .next = pos + 1 } };
}

// build a str from FRESH bytes (a decoded literal), not a view into the source.
// Escapes are ASCII and literal bytes come from the validated source, so validity
// holds; this is the bootstrap's `str(xs)` over decoded bytes. Allocation failure
// PANICS (abort — M.5), matching TK_LIST.
static tk_str str_of_bytes(const tk_byte *xs, size_t n) {
    tk_byte *buf = NULL;
    if (n > 0) {
        buf = tk_alloc(n);
        if (buf == NULL) abort();
        for (size_t i = 0; i < n; i += 1) buf[i] = xs[i];
    }
    tk_str_result r = tk_str_from_utf8(buf, n);
    if (!r.ok) abort();                       // decoded bytes are valid by construction
    return r.as.value;
}

static tk_str one_byte(tk_byte b) {
    return str_of_bytes(&b, 1);
}

// --- string & byte literals ---

// ---- the UNIFIED string reader: two ORTHOGONAL prefix modifiers × two delimiters ----
//
// A string START is an optional run of `$`/`@` (each at most once, ANY order: `$@` == `@$`)
// followed by the opening delimiter `"` (single-line) or `"""` (multi-line). The eight
// combinations are all valid; `$` and `@` COMPOSE freely (C# `$@"…"`).
//   `$`  = interpolation (holes `{expr}`, literal braces `{{`/`}}`).
//   `@`  = verbatim / raw  (NO escape processing — a `\` is a literal byte).
// The reader scans the prefix flags, the delimiter, then ONE unified body scanner
// parameterized by (has_interp, has_verbatim, multiline). Non-interp → fully-decoded Str;
// interp → Interp (parser decodes escapes) or InterpRaw (parser does NOT decode escapes).
//
// InterpRaw REPRESENTATION (the parser splits it the same way as Interp, but appends literal
// bytes verbatim): for SINGLE-line verbatim the lexer copies the inner text to FRESH bytes,
// applying the depth-0-outside-holes `""`→`"` collapse while leaving `\` and the hole `{…}`
// spans intact; for MULTI-line verbatim the inner text is copied AS-IS (a lone `"`/`""` is
// literal — only `"""` closes; NO `""`→`"` collapse). Thus the `""`-collapse lives ONLY here,
// in the lexer, so the parser can treat single- vs multi-line InterpRaw uniformly.
// For non-verbatim Interp the OLD raw-VIEW + parser-decodes-escapes path is unchanged.

// the closing delimiter matches at `p`? (three quotes if multiline, else one).
static bool str_close_at(tk_str source, size_t p, bool multiline) {
    if (source.ptr[p] != '"') return false;
    if (!multiline) return true;
    return at(source, p + 1) == '"' && at(source, p + 2) == '"';
}

// the UNIFIED string body scanner. `inner` is the first byte AFTER the opening delimiter;
// (has_interp, has_verbatim, multiline) select the four behaviours. Mirrors lexer.tks
// `read_string_body`.
static tk_scan_result read_string_body(tk_str source, size_t pos, size_t inner,
                                       bool has_interp, bool has_verbatim, bool multiline) {
    size_t delim = multiline ? 3 : 1;
    if (has_interp) {
        // INTERP: produce a token whose `.text` the parser will split into pieces + holes.
        if (!has_verbatim) {
            // plain interp → keep the existing zero-copy raw-VIEW; the parser decodes escapes.
            size_t p = inner;
            size_t depth = 0;                         // brace nesting inside holes
            for (;;) {
                if (p >= source.len) return scan_err_at(source, pos, "unterminated interpolated string");
                tk_byte c = source.ptr[p];
                if (!multiline && c == '\\') {        // an escape — skip the next byte too (single-line only)
                    if (p + 1 >= source.len) return scan_err_at(source, pos, "unterminated escape in interpolated string");
                    p += 2;
                    continue;
                }
                if (depth == 0 && c == '{' && at(source, p + 1) == '{') { p += 2; continue; }   // `{{` literal — depth unchanged
                if (depth == 0 && c == '}' && at(source, p + 1) == '}') { p += 2; continue; }   // `}}` literal — depth unchanged
                if (c == '{') { depth += 1; p += 1; continue; }
                if (c == '}') { if (depth > 0) depth -= 1; p += 1; continue; }
                if (depth == 0 && str_close_at(source, p, multiline)) {
                    return scan_ok((tk_scan){
                        .token = (tk_token){ .kind = TK_TOKEN_INTERP, .text = tk_str_slice(source, inner, p) },
                        .next  = p + delim,
                    });
                }
                p += 1;
            }
        }
        // VERBATIM interp → emit FRESH inner bytes (single-line collapses `""`→`"` at depth 0;
        // multi-line copies as-is). `\` stays literal; hole `{…}` spans are copied intact.
        size_t p = inner;
        size_t depth = 0;
        tk_lex_bytes bytes = tk_lex_bytes_empty();
        for (;;) {
            if (p >= source.len) { tk_lex_bytes_free(bytes); return scan_err_at(source, pos, "unterminated interpolated string"); }
            tk_byte c = source.ptr[p];
            if (depth == 0 && c == '{' && at(source, p + 1) == '{') {   // `{{` literal — copy BOTH bytes AS-IS, depth unchanged
                bytes = tk_lex_bytes_push(bytes, '{'); bytes = tk_lex_bytes_push(bytes, '{'); p += 2; continue;
            }
            if (depth == 0 && c == '}' && at(source, p + 1) == '}') {   // `}}` literal — copy BOTH bytes AS-IS, depth unchanged
                bytes = tk_lex_bytes_push(bytes, '}'); bytes = tk_lex_bytes_push(bytes, '}'); p += 2; continue;
            }
            if (c == '{') { depth += 1; bytes = tk_lex_bytes_push(bytes, c); p += 1; continue; }
            if (c == '}') { if (depth > 0) depth -= 1; bytes = tk_lex_bytes_push(bytes, c); p += 1; continue; }
            if (c == '"' && depth == 0) {
                if (!multiline) {
                    if (at(source, p + 1) == '"') {   // doubled quote → one literal `"`, continue
                        bytes = tk_lex_bytes_push(bytes, '"');
                        p += 2;
                        continue;
                    }
                    tk_str text = str_of_bytes(bytes.ptr, bytes.len);
                    tk_lex_bytes_free(bytes);
                    return scan_ok((tk_scan){ .token = (tk_token){ .kind = TK_TOKEN_INTERP_RAW, .text = text }, .next = p + 1 });
                }
                if (str_close_at(source, p, true)) {  // only `"""` closes; a lone `"`/`""` is literal
                    tk_str text = str_of_bytes(bytes.ptr, bytes.len);
                    tk_lex_bytes_free(bytes);
                    return scan_ok((tk_scan){ .token = (tk_token){ .kind = TK_TOKEN_INTERP_RAW, .text = text }, .next = p + 3 });
                }
            }
            bytes = tk_lex_bytes_push(bytes, c);      // every other byte (incl `\` and `\n`) is literal
            p += 1;
        }
    }
    // NON-INTERP: fully decode to a Str token.
    size_t p = inner;
    tk_lex_bytes bytes = tk_lex_bytes_empty();
    for (;;) {
        if (p >= source.len) {
            tk_lex_bytes_free(bytes);
            return scan_err_at(source, pos, "unterminated string literal");
        }
        tk_byte c = source.ptr[p];
        if (has_verbatim) {
            // verbatim: NO escapes. SINGLE-line `""`→`"` (so a doubled quote is content, NOT a
            // close — must be checked BEFORE str_close_at); multi-line a lone `"`/`""` is literal.
            if (!multiline && c == '"' && at(source, p + 1) == '"') {
                bytes = tk_lex_bytes_push(bytes, '"');
                p += 2;
                continue;
            }
            if (str_close_at(source, p, multiline)) {
                tk_str text = str_of_bytes(bytes.ptr, bytes.len);
                tk_lex_bytes_free(bytes);
                return scan_ok((tk_scan){ .token = (tk_token){ .kind = TK_TOKEN_STR, .text = text }, .next = p + delim });
            }
            bytes = tk_lex_bytes_push(bytes, c);      // every other byte (incl `\` and `\n`) is literal
            p++;
            continue;
        }
        if (str_close_at(source, p, multiline)) {
            tk_str text = str_of_bytes(bytes.ptr, bytes.len);
            tk_lex_bytes_free(bytes);
            return scan_ok((tk_scan){
                .token = (tk_token){ .kind = TK_TOKEN_STR, .text = text },
                .next  = p + delim,
            });
        }
        // plain (escapes on).
        if (c == '\\') {
            tk_esc_result e = escape_byte(source, p);
            if (!e.ok) { tk_lex_bytes_free(bytes); return scan_err(e.as.error.message); }
            bytes = tk_lex_bytes_push(bytes, e.as.value.value);
            p = e.as.value.next;
            continue;
        }
        bytes = tk_lex_bytes_push(bytes, c);          // a literal byte (ASCII or UTF-8 cont.)
        p++;
    }
}

// recognize a string START at `pos`: an optional run of `$`/`@` (each ≤1, any order) then a
// `"`. On match, scan the prefix flags + delimiter and dispatch to read_string_body. The
// caller only routes here once is_string_start confirmed a `"` follows the prefix run.
// Mirrors lexer.tks `read_string`.
static tk_scan_result read_string(tk_str source, size_t pos) {
    bool has_interp = false, has_verbatim = false;
    size_t p = pos;
    for (;;) {
        tk_byte c = at(source, p);
        if (c == '$') { if (has_interp) break; has_interp = true; p += 1; continue; }
        if (c == '@') { if (has_verbatim) break; has_verbatim = true; p += 1; continue; }
        break;
    }
    bool multiline = at(source, p + 1) == '"' && at(source, p + 2) == '"';
    size_t inner = p + (multiline ? 3 : 1);
    return read_string_body(source, pos, inner, has_interp, has_verbatim, multiline);
}

// is there a string START at `pos`? an optional `$`/`@` prefix run (each ≤1, any order) then a
// `"`. Mirrors lexer.tks `is_string_start`.
static bool is_string_start(tk_str source, size_t pos) {
    bool has_interp = false, has_verbatim = false;
    size_t p = pos;
    for (;;) {
        tk_byte c = at(source, p);
        if (c == '$') { if (has_interp) break; has_interp = true; p += 1; continue; }
        if (c == '@') { if (has_verbatim) break; has_verbatim = true; p += 1; continue; }
        break;
    }
    return at(source, p) == '"';
}

// `b'…'`: pos points at `b`, `'` at pos+1. One byte (raw or escaped) then a closing `'`.
static tk_scan_result read_byte_lit(tk_str source, size_t pos) {
    size_t inner = pos + 2;                       // past `b'`
    if (inner >= source.len) return scan_err("unterminated byte literal");
    tk_byteval_result v = byte_value(source, inner);
    if (!v.ok) return scan_err(v.as.error.message);
    if (at(source, v.as.value.next) != '\'') {
        return scan_err("expected closing ' in byte literal");
    }
    return scan_ok((tk_scan){
        .token = (tk_token){ .kind = TK_TOKEN_BYTE, .text = one_byte(v.as.value.value) },
        .next  = v.as.value.next + 1,
    });
}

// expected UTF-8 codepoint length (1–4) from the LEAD byte, or 0 if `b` is not a valid lead.
// Mirrors the lead-byte classification of text.c's valid_utf8 (RFC 3629 ranges).
static size_t utf8_codepoint_len(tk_byte b) {
    if (b <= 0x7F) return 1;                      // ASCII — single byte
    if (b >= 0xC2 && b <= 0xDF) return 2;
    if (b >= 0xE0 && b <= 0xEF) return 3;
    if (b >= 0xF0 && b <= 0xF4) return 4;
    return 0;                                     // 0x80..0xC1, 0xF5..0xFF — never a valid lead
}

// `c'…'`: pos points at `c`, `'` at pos+1. Collects bytes (each raw byte OR an escape decoded to
// one ASCII byte) up to a closing `'`, then VALIDATES they form EXACTLY ONE UTF-8 codepoint.
// `c'A'`=1 byte, `c'é'`=2, `c'😀'`=4, `c'\n'`=one escaped byte. The token's text is the raw UTF-8.
static tk_scan_result read_char_lit(tk_str source, size_t pos) {
    size_t inner = pos + 2;                       // past `c'`
    if (inner >= source.len) return scan_err("unterminated char literal");
    tk_lex_bytes bytes = tk_lex_bytes_empty();
    size_t p = inner;
    for (;;) {
        if (p >= source.len) { tk_lex_bytes_free(bytes); return scan_err("expected closing ' in char literal"); }
        if (source.ptr[p] == '\'') break;         // closing quote
        tk_byteval_result v = byte_value(source, p);
        if (!v.ok) { tk_lex_bytes_free(bytes); return scan_err(v.as.error.message); }
        bytes = tk_lex_bytes_push(bytes, v.as.value.value);
        p = v.as.value.next;
    }
    if (bytes.len == 0) { tk_lex_bytes_free(bytes); return scan_err("empty char literal — a char must be exactly one codepoint"); }
    tk_str_result r = tk_str_from_utf8(bytes.ptr, bytes.len);   // validates UTF-8 (zero-copy view on success)
    if (!r.ok) { tk_lex_bytes_free(bytes); return scan_err("invalid UTF-8 in char literal"); }
    if (utf8_codepoint_len(bytes.ptr[0]) != bytes.len) {
        tk_lex_bytes_free(bytes);
        return scan_err("char literal must be exactly one codepoint");
    }
    tk_str text = str_of_bytes(bytes.ptr, bytes.len);          // FRESH bytes (own the codepoint)
    tk_lex_bytes_free(bytes);
    return scan_ok((tk_scan){
        .token = (tk_token){ .kind = TK_TOKEN_CHAR, .text = text },
        .next  = p + 1,
    });
}

// --- symbols: operators, delimiters, punctuation (maximal munch — B.23) ---

static tk_scan_result read_symbol(tk_str source, size_t pos) {
    tk_byte c  = source.ptr[pos];
    tk_byte c1 = at(source, pos + 1);
    tk_byte c2 = at(source, pos + 2);

    // 3-byte (longest first)
    if (c == '<' && c1 == '<' && c2 == '=') return scan_ok(sym(source, pos, 3, TK_TOKEN_SHLEQ));
    if (c == '>' && c1 == '>' && c2 == '=') return scan_ok(sym(source, pos, 3, TK_TOKEN_SHREQ));
    if (c == '.' && c1 == '.' && c2 == '=') return scan_ok(sym(source, pos, 3, TK_TOKEN_DOTDOTEQ));

    // 2-byte
    if (c == '=' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_EQEQ));
    if (c == '=' && c1 == '>') return scan_ok(sym(source, pos, 2, TK_TOKEN_FATARROW));
    if (c == '!' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_NE));
    if (c == '<' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_LE));
    if (c == '>' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_GE));
    if (c == '<' && c1 == '<') return scan_ok(sym(source, pos, 2, TK_TOKEN_SHL));
    if (c == '>' && c1 == '>') return scan_ok(sym(source, pos, 2, TK_TOKEN_SHR));
    if (c == '-' && c1 == '>') return scan_ok(sym(source, pos, 2, TK_TOKEN_ARROW));
    if (c == ':' && c1 == ':') return scan_ok(sym(source, pos, 2, TK_TOKEN_COLONCOLON));
    if (c == '&' && c1 == '&') return scan_ok(sym(source, pos, 2, TK_TOKEN_ANDAND));
    if (c == '|' && c1 == '|') return scan_ok(sym(source, pos, 2, TK_TOKEN_OROR));
    if (c == '+' && c1 == '+') return scan_ok(sym(source, pos, 2, TK_TOKEN_PLUSPLUS));     // ++ (maximal munch, before +/+=)
    if (c == '-' && c1 == '-') return scan_ok(sym(source, pos, 2, TK_TOKEN_MINUSMINUS));   // -- (before -/-=/->)
    if (c == '+' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_PLUSEQ));
    if (c == '-' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_MINUSEQ));
    if (c == '*' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_STAREQ));
    if (c == '/' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_SLASHEQ));
    if (c == '%' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_PERCENTEQ));
    if (c == '&' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_AMPEQ));
    if (c == '|' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_PIPEEQ));
    if (c == '^' && c1 == '=') return scan_ok(sym(source, pos, 2, TK_TOKEN_CARETEQ));

    // nullability composites (maximal munch — `??`/`?.` before a lone `?`).
    // `?` is EXCLUSIVE to nullability (LEGISLATION §75; REBOOT_PLAN §202–203).
    if (c == '?' && c1 == '?') return scan_ok(sym(source, pos, 2, TK_TOKEN_QQ));
    if (c == '?' && c1 == '.') return scan_ok(sym(source, pos, 2, TK_TOKEN_QDOT));

    // `..` — bare range / spread (C6.7); `..=` is already consumed above by the 3-byte check.
    if (c == '.' && c1 == '.') return scan_ok(sym(source, pos, 2, TK_TOKEN_DOTDOT));

    // 1-byte
    tk_token_kind one;
    switch (c) {
        case '+': one = TK_TOKEN_PLUS;      break;
        case '-': one = TK_TOKEN_MINUS;     break;
        case '*': one = TK_TOKEN_STAR;      break;
        case '/': one = TK_TOKEN_SLASH;     break;
        case '%': one = TK_TOKEN_PERCENT;   break;
        case '&': one = TK_TOKEN_AMP;       break;
        case '|': one = TK_TOKEN_PIPE;      break;
        case '^': one = TK_TOKEN_CARET;     break;
        case '~': one = TK_TOKEN_TILDE;     break;
        case '!': one = TK_TOKEN_BANG;      break;
        case '<': one = TK_TOKEN_LT;        break;
        case '>': one = TK_TOKEN_GT;        break;
        case '=': one = TK_TOKEN_ASSIGN;    break;
        case ':': one = TK_TOKEN_COLON;     break;
        case ';': one = TK_TOKEN_SEMICOLON; break;
        case ',': one = TK_TOKEN_COMMA;     break;
        case '.': one = TK_TOKEN_DOT;       break;
        case '(': one = TK_TOKEN_LPAREN;    break;
        case ')': one = TK_TOKEN_RPAREN;    break;
        case '{': one = TK_TOKEN_LBRACE;    break;
        case '}': one = TK_TOKEN_RBRACE;    break;
        case '[': one = TK_TOKEN_LBRACKET;  break;
        case ']': one = TK_TOKEN_RBRACKET;  break;
        case '?': one = TK_TOKEN_QUESTION;  break;   // lone `?` — the type-suffix (T?)
        case '#': one = TK_TOKEN_HASH;      break;   // attribute marker (`#test`) — D2 test gate
        default:  return scan_err_at(source, pos, "unexpected character");
    }
    return scan_ok(sym(source, pos, 1, one));
}

// --- one token at pos (already past spaces/comments/newline), or fail ---

static tk_scan_result next_token(tk_str source, size_t pos) {
    tk_byte c = source.ptr[pos];
    // a byte literal `b'…'` — `b` would otherwise begin an identifier
    if (c == 'b' && at(source, pos + 1) == '\'') return read_byte_lit(source, pos);
    // a char literal `c'…'` — `c` would otherwise begin an identifier
    if (c == 'c' && at(source, pos + 1) == '\'') return read_char_lit(source, pos);
    // a string literal: an optional `$`/`@` prefix run (each ≤1, any order) then `"` / `"""`.
    // Covers `"`, `$"`, `@"`, `$@"`, `@$"` and the `"""` variants — the unified reader. A bare
    // `$`/`@` not followed (after the prefix run) by a quote is NOT a string start (matched=false).
    if ((c == '"' || c == '$' || c == '@') && is_string_start(source, pos)) {
        return read_string(source, pos);
    }
    if (is_digit(c)) return scan_ok(read_number(source, pos));
    if (is_alpha(c)) return scan_ok(keyword_or_ident(source, pos));
    if (c == '_')    return scan_ok(read_underscore(source, pos));
    return read_symbol(source, pos);
}

// --- the main loop (lexer.tks `tokenize`) ---

tk_tokens_result tk_tokenize(tk_str source) {
    size_t pos = 0;
    tk_tokens tokens = tk_tokens_empty();
    // location cursor: (cur_line, cur_col) is the 1-based location of byte `cur_pos`. It
    // advances forward to each token-start `pos`; since the token starts are monotonic, the
    // total advance work is O(N) (each byte crossed once) — replacing the old O(N²) stamping.
    size_t cur_pos = 0;
    uint32_t cur_line = 1, cur_col = 1;

    for (;;) {
        pos = skip_spaces(source, pos);
        if (pos >= source.len) break;

        // advance the cursor from cur_pos up to the token start `pos`, counting newlines.
        for (; cur_pos < pos; cur_pos += 1) {
            if (source.ptr[cur_pos] == '\n') { cur_line += 1; cur_col = 1; } else { cur_col += 1; }
        }

        tk_byte c = source.ptr[pos];

        // `//` line comment → skip to the newline
        if (c == '/' && at(source, pos + 1) == '/') {
            pos = skip_line(source, pos);
            continue;
        }
        // `/* … */` block & `/** … */` doc comments
        if (c == '/' && at(source, pos + 1) == '*') {
            // a DOC comment `/** … */` (but NOT the empty `/**/`) becomes a Doc TOKEN;
            // a plain block comment is skipped.
            if (at(source, pos + 2) == '*' && at(source, pos + 3) != '/') {
                tk_scan_result sc = read_doc_comment(source, pos);
                if (!sc.ok) { tk_tokens_free(tokens); return (tk_tokens_result){ .ok = false, .as.error = sc.as.error }; }
                tokens = tk_tokens_push(tokens, stamp_loc(sc.as.value.token, cur_line, cur_col));
                pos = sc.as.value.next;
                continue;
            }
            tk_pos_result bp = skip_block_comment(source, pos);
            if (!bp.ok) { tk_tokens_free(tokens); return (tk_tokens_result){ .ok = false, .as.error = bp.as.error }; }
            pos = bp.as.value;
            continue;
        }
        // a significant newline (B.26) is a token
        if (c == '\n') {
            tokens = tk_tokens_push(tokens, stamp_loc(sym(source, pos, 1, TK_TOKEN_NEWLINE).token, cur_line, cur_col));
            pos = pos + 1;
            continue;
        }

        tk_scan_result sc = next_token(source, pos);
        if (!sc.ok) { tk_tokens_free(tokens); return (tk_tokens_result){ .ok = false, .as.error = sc.as.error }; }
        tokens = tk_tokens_push(tokens, stamp_loc(sc.as.value.token, cur_line, cur_col));
        pos    = sc.as.value.next;
    }

    return (tk_tokens_result){ .ok = true, .as.value = tokens };
}

// (The literal decoders lit_int / lit_byte are parser-namespace fns — RELOCATED to
//  src/parser/parse_lit.{c,h} per parse_lit.tks. They were never called by the lexer.)
