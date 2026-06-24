// src/lexer/token.h — teko::lexer token TYPES, the C23 mirror of token.tks.
//
// Faithful mirror of the Teko `TokenKind` enum + `Token` struct. The enum order
// MATCHES token.tks so operator kinds serialize by their enum ordinal (E7's
// `kind_byte`); the word/punct kinds the delta appends (To/Dot/Semicolon/Variant)
// stay LAST so existing `.tkb` byte ordinals are never shifted.
#ifndef TK_LEXER_TOKEN_H
#define TK_LEXER_TOKEN_H

#include "../text/text.h"   // tk_str (DAG: lexer → text → core)

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
    TK_TOKEN_LET,
    TK_TOKEN_MUT,
    TK_TOKEN_CONST,
    TK_TOKEN_IF,
    TK_TOKEN_ELSE,
    TK_TOKEN_LOOP,
    TK_TOKEN_BREAK,
    TK_TOKEN_CONTINUE,
    TK_TOKEN_RETURN,
    TK_TOKEN_MATCH,
    TK_TOKEN_WHEN,
    TK_TOKEN_AS,
    TK_TOKEN_USE,
    TK_TOKEN_EXP,

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
} tk_token_kind;

// tk_token — mirrors token.tks `Token`: a kind + the source text span (a str VIEW
// for most kinds; a FRESH decoded str for Str/Byte literals).
typedef struct {
    tk_token_kind kind;
    tk_str        text;
} tk_token;

#endif // TK_LEXER_TOKEN_H
