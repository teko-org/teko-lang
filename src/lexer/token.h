// src/lexer/token.h — teko::lexer token TYPES, the C23 mirror of token.tks.
//
// Faithful mirror of the Teko `TokenKind` enum + `Token` struct. The enum order
// MATCHES token.tks so operator kinds serialize by their enum ordinal (E7's
// `kind_byte`); the word/punct kinds the delta appends (To/Dot/Semicolon/Variant)
// stay LAST so existing `.tkb` byte ordinals are never shifted.
#ifndef TK_LEXER_TOKEN_H
#define TK_LEXER_TOKEN_H

#include "../text/text.h"   // tk_str (DAG: lexer → text → core)
#include <stdint.h>         // uint32_t (token line/col)

// tk_token_kind — mirrors token.tks `TokenKind`, in the SAME order.
typedef enum {
    // --- literals & identifiers ---
    TK_TOKEN_NUMBER,        // 123, 1_000
    TK_TOKEN_IDENT,         // snake_case names
    TK_TOKEN_STR,           // "…"
    TK_TOKEN_BYTE,          // b'x' — one octet
    TK_TOKEN_UNDERSCORE,    // _ — the wildcard

    // --- keywords ---
    TK_TOKEN_FN,
    TK_TOKEN_TYPE,
    TK_TOKEN_STRUCT,
    TK_TOKEN_ENUM,
    TK_TOKEN_FLAGS,
    TK_TOKEN_LET,
    TK_TOKEN_MUT,
    TK_TOKEN_CONST,
    TK_TOKEN_IF,
    TK_TOKEN_ELSE,
    TK_TOKEN_LOOP,
    TK_TOKEN_BREAK,
    TK_TOKEN_CONTINUE,
    TK_TOKEN_RETURN,
    TK_TOKEN_DEFER,    // `defer` — scoped cleanup block (C7.18)
    TK_TOKEN_MATCH,
    TK_TOKEN_WHEN,
    TK_TOKEN_AS,
    TK_TOKEN_USE,
    TK_TOKEN_EXP,
    TK_TOKEN_TRUE,          // `true`  — bool literal (LEGISLATION §75)
    TK_TOKEN_FALSE,         // `false` — bool literal (LEGISLATION §75)
    TK_TOKEN_NULL,          // `null`  — the null literal (REBOOT_PLAN §202)

    // --- arithmetic & bitwise operators ---
    TK_TOKEN_PLUS,          // +
    TK_TOKEN_MINUS,         // -
    TK_TOKEN_STAR,          // *
    TK_TOKEN_SLASH,         // /
    TK_TOKEN_PERCENT,       // %
    TK_TOKEN_AMP,           // &
    TK_TOKEN_PIPE,          // |
    TK_TOKEN_CARET,         // ^
    TK_TOKEN_TILDE,         // ~
    TK_TOKEN_SHL,           // <<
    TK_TOKEN_SHR,           // >>

    // --- assignment (compound — B.4: statement-only) ---
    TK_TOKEN_ASSIGN,        // =
    TK_TOKEN_PLUSEQ,        // +=
    TK_TOKEN_MINUSEQ,       // -=
    TK_TOKEN_STAREQ,        // *=
    TK_TOKEN_SLASHEQ,       // /=
    TK_TOKEN_PERCENTEQ,     // %=
    TK_TOKEN_AMPEQ,         // &=
    TK_TOKEN_PIPEEQ,        // |=
    TK_TOKEN_CARETEQ,       // ^=
    TK_TOKEN_SHLEQ,         // <<=
    TK_TOKEN_SHREQ,         // >>=

    // --- comparison ---
    TK_TOKEN_EQEQ,          // ==
    TK_TOKEN_NE,            // !=
    TK_TOKEN_LT,            // <
    TK_TOKEN_LE,            // <=
    TK_TOKEN_GT,            // >
    TK_TOKEN_GE,            // >=

    // --- logical ---
    TK_TOKEN_ANDAND,        // &&
    TK_TOKEN_OROR,          // ||
    TK_TOKEN_BANG,          // !

    // --- delimiters & punctuation ---
    TK_TOKEN_LPAREN,        // (
    TK_TOKEN_RPAREN,        // )
    TK_TOKEN_LBRACE,        // {
    TK_TOKEN_RBRACE,        // }
    TK_TOKEN_LBRACKET,      // [
    TK_TOKEN_RBRACKET,      // ]
    TK_TOKEN_COMMA,         // ,
    TK_TOKEN_COLON,         // :
    TK_TOKEN_COLONCOLON,    // ::
    TK_TOKEN_ARROW,         // ->
    TK_TOKEN_FATARROW,      // =>
    TK_TOKEN_DOTDOTEQ,      // ..=
    TK_TOKEN_NEWLINE,       // a significant line break (B.26 — statement separator)
    TK_TOKEN_DOC,           // /** … */ doc comment (attaches to a type/fn declaration)

    // --- word-operator (B.23) — APPENDED LAST on purpose: operator kinds serialize
    //     by enum ordinal (E7's kind_byte), so a new variant must not shift the
    //     existing operators. These four keep the existing .tkb ordinals stable.
    TK_TOKEN_TO,            // `to` — the cast operator (x to T); preserves→ok / loses→error (E7)
    TK_TOKEN_DOT,           // `.` — postfix field/method access (P2/F2)
    TK_TOKEN_SEMICOLON,     // `;` — statement/field separator (B.17)
    TK_TOKEN_VARIANT,       // `variant` — variant type-body keyword (B.14)

    // --- nullability (`?` is EXCLUSIVE to nullability — LEGISLATION §75; REBOOT_PLAN
    //     §202–203). APPENDED LAST, same ordinal-stability reason as the word-ops.
    TK_TOKEN_QUESTION,      // `?`  — type suffix: `T?` is an OPTIONAL type
    TK_TOKEN_QDOT,          // `?.` — safe field access (null-propagating)
    TK_TOKEN_QQ,            // `??` — Elvis / null-coalescing

    // --- visibility (LEGISLATION "Visibility — pub vs exp"; B.9). `pub` APPENDED LAST,
    //     same ordinal-stability reason as the word-ops/`variant` above: operator kinds
    //     serialize by enum ordinal (E7's kind_byte), so a mid-table insert would shift
    //     them. `exp` predates this rule and sits in the keyword block; `pub` joins here
    //     (it is never a stored op, so its ordinal is never serialized).
    TK_TOKEN_PUB,           // `pub` — public within the project (visible cross-namespace)

    // --- increment/decrement (W5-idx self-host). APPENDED LAST (ordinal stability). These
    //     are STATEMENT sugar: `i++` desugars to `i += 1`, `i--` to `i -= 1` at parse time,
    //     so they need no checker/codegen/VM node (they reuse compound assignment).
    TK_TOKEN_PLUSPLUS,      // `++` — postfix increment (statement)
    TK_TOKEN_MINUSMINUS,    // `--` — postfix decrement (statement)

    // --- string interpolation `$"…{expr}…"` (self-host parity). APPENDED LAST
    //     (ordinal stability — operator kinds serialize by enum ordinal; INTERP is never a
    //     stored op, so its ordinal is never serialized). The token's `.text` is the RAW
    //     inner content between the quotes (holes + escapes still encoded); the PARSER
    //     splits it into literal pieces + hole expressions (parse_expr's INTERP case).
    TK_TOKEN_INTERP,        // `$"…{expr}…"` — an interpolated string (raw inner text)

    // --- membership (`in` operator — Phase 2). APPENDED LAST (ordinal stability — operator
    //     kinds serialize by enum ordinal; `in` is never a stored op, so its ordinal is never
    //     serialized). `<expr> in [ e0, e1, … ]` → bool: true iff the LHS equals any element.
    //     The `[ … ]` is a SPECIAL membership-set syntax valid ONLY as the `in` RHS (there is
    //     no general array literal). Comparison-precedence; does NOT chain.
    TK_TOKEN_IN,            // `in` — the membership operator (x in [ … ])
    TK_TOKEN_HASH,          // `#` — attribute marker (`#test`) — D2 test gate

    // --- FFI (`extern` — C7.1a). APPENDED LAST (ordinal stability — never a stored op, so its
    //     ordinal is never serialized). `extern fn name(params) -> ret = "sym" from "lib"`
    //     declares a foreign C function (no body). `from` is NOT reserved — it stays a normal
    //     identifier (the corpus uses `from` as a param name), matched CONTEXTUALLY by the parser
    //     in the extern position. See LEGISLATION §"FFI / `extern`".
    TK_TOKEN_EXTERN,        // `extern` — foreign-function declarator

    // --- bare range / spread (C6.7). APPENDED LAST (ordinal stability — never a stored op, so
    //     its ordinal is never serialized). `..` without a trailing `=` is a distinct token used
    //     for exclusive ranges and spread expressions. `..=` (inclusive range) is still
    //     TK_TOKEN_DOTDOTEQ and is matched first by the 3-byte maximal-munch check.
    TK_TOKEN_DOTDOT,        // `..` — bare range / spread (exclusive; `..=` remains TK_TOKEN_DOTDOTEQ)

    // --- VERBATIM interpolation `$@"…"` / `@$"…"` (orthogonal modifiers). APPENDED LAST
    //     (ordinal stability — never a stored op, so its ordinal is never serialized). Like
    //     TK_TOKEN_INTERP, but the `@` (verbatim) modifier is also present: the parser splits
    //     the inner text into pieces + holes the SAME way, but appends literal bytes VERBATIM
    //     (no escape decoding). The lexer has already resolved single- vs multi-line `""`
    //     handling into the token's FRESH `.text` (single-line `""`→`"` collapse applied;
    //     multi-line copied as-is), so the parser treats the two uniformly.
    TK_TOKEN_INTERP_RAW,    // `$@"…{expr}…"` — a verbatim interpolated string (fresh inner bytes)
    // char literal `c'…'` (UTF-8 codepoint). Appended LAST for ordinal stability (a Char token is
    // never a stored operator, so its ordinal is never serialized to `.tkb`). Its `.text` is the
    // codepoint's raw UTF-8 bytes (1–4), already validated by the lexer.
    TK_TOKEN_CHAR,          // `c'x'` — one UTF-8 codepoint (1–4 bytes)
    // W10b.CLASS (2026-07-01) — the class construct. Appended LAST (ordinal stability). `self`/
    // `base` deliberately NOT reserved — see token.tks's matching note.
    TK_TOKEN_CLASS,         // `class` — the class construct (sealed/final by default)
    TK_TOKEN_ABSTRACT,      // `abstract class` — inheritable, NOT instantiable
    TK_TOKEN_VIRTUAL,       // `virtual class` / `virtual fn` — instantiable+inheritable class / an overridable method
    TK_TOKEN_OVERRIDE,      // `override fn` — overrides a base virtual/abstract method
    TK_TOKEN_INTERN,        // `intern` — visibility: visible to inheritors
} tk_token_kind;
// `params` — variadic-parameter modifier (2026-07-01 ruling) is NOT a reserved token: like `from`
// (FFI `extern` position), it stays a normal TK_TOKEN_IDENT and is matched CONTEXTUALLY by the
// parser at the parameter-declaration position (`text_is(t[p].text, "params")`). A real reserved
// keyword would collide with the ~295 existing uses of `params` as a plain identifier/field name
// across the self-hosted corpus (e.g. `gf.params`, `lam.params`) — see parse_decl.c/.tks.

// tk_token — mirrors token.tks `Token`: a kind + the source text span (a str VIEW
// for most kinds; a FRESH decoded str for Str/Byte literals) + its 1-based source
// LINE/COLUMN (stamped by the lexer — drives file:line:col diagnostics; M.3 honest).
typedef struct {
    tk_token_kind kind;
    tk_str        text;
    uint32_t      line;   // 1-based source line  (0 = unstamped)
    uint32_t      col;    // 1-based source column
} tk_token;

#endif // TK_LEXER_TOKEN_H
