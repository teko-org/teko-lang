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
// stamp a token with the 1-based location of its start byte `pos`.
static tk_token stamp(tk_token tok, tk_str source, size_t pos) {
    compute_loc(source, pos, &tok.line, &tok.col);
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
    if (tk_str_eq_lit(text, "match"))    return TK_TOKEN_MATCH;
    if (tk_str_eq_lit(text, "when"))     return TK_TOKEN_WHEN;
    if (tk_str_eq_lit(text, "in"))       return TK_TOKEN_IN;    // the membership operator (x in [ … ]) — Phase 2
    if (tk_str_eq_lit(text, "as"))       return TK_TOKEN_AS;
    if (tk_str_eq_lit(text, "to"))       return TK_TOKEN_TO;   // the cast operator (x to T) — F1/E7
    if (tk_str_eq_lit(text, "use"))      return TK_TOKEN_USE;
    if (tk_str_eq_lit(text, "pub"))      return TK_TOKEN_PUB;   // public within the project (B.9)
    if (tk_str_eq_lit(text, "exp"))      return TK_TOKEN_EXP;
    if (tk_str_eq_lit(text, "true"))     return TK_TOKEN_TRUE;   // bool literal (LEGISLATION §75)
    if (tk_str_eq_lit(text, "false"))    return TK_TOKEN_FALSE;  // bool literal (LEGISLATION §75)
    if (tk_str_eq_lit(text, "null"))     return TK_TOKEN_NULL;   // null literal (REBOOT_PLAN §202)
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

// `"` at pos. Collect bytes until the closing `"`, decoding escapes.
static tk_scan_result read_str(tk_str source, size_t pos) {
    size_t p = pos + 1;                          // past the opening quote
    tk_lex_bytes bytes = tk_lex_bytes_empty();           // TK_LIST over tk_byte (text.h via core)
    for (;;) {
        if (p >= source.len) {
            tk_lex_bytes_free(bytes);
            return scan_err("unterminated string literal");
        }
        tk_byte c = source.ptr[p];
        if (c == '"') {
            tk_str text = str_of_bytes(bytes.ptr, bytes.len);
            tk_lex_bytes_free(bytes);
            return scan_ok((tk_scan){
                .token = (tk_token){ .kind = TK_TOKEN_STR, .text = text },
                .next  = p + 1,
            });
        }
        if (c == '\\') {
            tk_esc_result e = escape_byte(source, p);
            if (!e.ok) { tk_lex_bytes_free(bytes); return scan_err(e.as.error.message); }
            bytes = tk_lex_bytes_push(bytes, e.as.value.value);
            p = e.as.value.next;
            continue;
        }
        bytes = tk_lex_bytes_push(bytes, c);         // a literal byte (ASCII or UTF-8 cont.)
        p++;
    }
}

// `$"…"` at pos (`$` at pos, `"` at pos+1). Scan the RAW inner text between the quotes —
// holes `{…}` and escapes are LEFT ENCODED; the parser splits the raw span into literal
// pieces + hole expressions (parse_expr's INTERP case). The token's `.text` is a VIEW into
// the source (like Str's span before decode; the parser decodes each literal piece). A `"`
// terminates ONLY at brace-depth 0 (so a string literal inside a hole would not end it),
// and `\"` is skipped (an escaped quote stays inside the content). Mirrors lexer.tks
// `read_interp`.
static tk_scan_result read_interp(tk_str source, size_t pos) {
    size_t start = pos + 2;                       // past `$"` — the first inner byte
    size_t p = start;
    size_t depth = 0;                             // brace nesting inside holes
    for (;;) {
        if (p >= source.len) return scan_err_at(source, pos, "unterminated interpolated string");
        tk_byte c = source.ptr[p];
        if (c == '\\') {                          // an escape — skip the next byte too
            if (p + 1 >= source.len) return scan_err_at(source, pos, "unterminated escape in interpolated string");
            p += 2;
            continue;
        }
        if (c == '{') { depth += 1; p += 1; continue; }
        if (c == '}') { if (depth > 0) depth -= 1; p += 1; continue; }
        if (c == '"' && depth == 0) {             // the closing quote (outside any hole)
            return scan_ok((tk_scan){
                .token = (tk_token){ .kind = TK_TOKEN_INTERP, .text = tk_str_slice(source, start, p) },
                .next  = p + 1,
            });
        }
        p += 1;
    }
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

    // `..` exists ONLY as `..=` (handled above); a lone `..` is an error
    if (c == '.' && c1 == '.') return scan_err("expected '=' to close range '..='");

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
    // an interpolated string `$"…"` — `$` is otherwise an unexpected character
    if (c == '$' && at(source, pos + 1) == '"') return read_interp(source, pos);
    if (is_digit(c)) return scan_ok(read_number(source, pos));
    if (is_alpha(c)) return scan_ok(keyword_or_ident(source, pos));
    if (c == '_')    return scan_ok(read_underscore(source, pos));
    if (c == '"')    return read_str(source, pos);
    return read_symbol(source, pos);
}

// --- the main loop (lexer.tks `tokenize`) ---

tk_tokens_result tk_tokenize(tk_str source) {
    size_t pos = 0;
    tk_tokens tokens = tk_tokens_empty();

    for (;;) {
        pos = skip_spaces(source, pos);
        if (pos >= source.len) break;

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
                tokens = tk_tokens_push(tokens, stamp(sc.as.value.token, source, pos));
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
            tokens = tk_tokens_push(tokens, stamp(sym(source, pos, 1, TK_TOKEN_NEWLINE).token, source, pos));
            pos = pos + 1;
            continue;
        }

        tk_scan_result sc = next_token(source, pos);
        if (!sc.ok) { tk_tokens_free(tokens); return (tk_tokens_result){ .ok = false, .as.error = sc.as.error }; }
        tokens = tk_tokens_push(tokens, stamp(sc.as.value.token, source, pos));
        pos    = sc.as.value.next;
    }

    return (tk_tokens_result){ .ok = true, .as.value = tokens };
}

// (The literal decoders lit_int / lit_byte are parser-namespace fns — RELOCATED to
//  src/parser/parse_lit.{c,h} per parse_lit.tks. They were never called by the lexer.)
