#ifndef LEXER_H
#define LEXER_H

#include <stdbool.h>

// Enumeration of all token types in the language
typedef enum {
    // End of file / Error
    TOKEN_EOF = 0,
    TOKEN_UNKNOWN,

    // Keywords
    TOKEN_USE, TOKEN_FROM, TOKEN_EXTERN, TOKEN_STRUCT, TOKEN_FN, TOKEN_AS,
    TOKEN_CONST, TOKEN_ASYNC, TOKEN_LET, TOKEN_MUT, TOKEN_DEFER, TOKEN_RETURN,
    TOKEN_WHEN, TOKEN_FOR, TOKEN_SWITCH, TOKEN_NULL, TOKEN_RAISED, TOKEN_WHERE,
    TOKEN_EXP, TOKEN_INTERFACE, TOKEN_SERVICE, TOKEN_DECORATES, TOKEN_EXTEND,
    TOKEN_COMMAND, TOKEN_HANDLER, TOKEN_NOTIFICATION, TOKEN_WITH, TOKEN_QUERY,
    TOKEN_OPERATOR, TOKEN_PUB, TOKEN_REQD,

    // Phase 12 — reserved keyword matrix (recognition only; lowering lands in the
    // respective feature phases 13–16).
    // Resilience:
    TOKEN_CIRCUIT, TOKEN_FALLBACK, TOKEN_DELAYED, TOKEN_RETRY,
    TOKEN_EXPONENTIAL, TOKEN_LOGARITHMIC, TOKEN_ATTEMPTS, TOKEN_TIMEOUT,
    // OOP & concurrency:
    TOKEN_CLASS, TOKEN_ABSTRACT, TOKEN_TRAIT, TOKEN_EVENT, TOKEN_RAISE,
    TOKEN_SUBSCRIBE, TOKEN_FANOUT, TOKEN_FIRE_AND_FORGET, TOKEN_SHARED,
    TOKEN_ATOMIC, TOKEN_ROUTINES, TOKEN_DUPLEX,
    // Phase 14 (14.G) — timespan waiters: `wait <ts>;` (sync sleep) / `await <ts>;`
    // (cooperative timed yield). Timespan literals (10ms/2s/…) already lex (Phase 12 unit).
    TOKEN_WAIT, TOKEN_AWAIT,
    // Phase 14 (control-flow foundation) — structured loops + branches lowered from source:
    // `while (cond) { }`, `loop { }`, `if (cond) { }`, `break;`, `continue;`.
    TOKEN_WHILE, TOKEN_LOOP, TOKEN_IF, TOKEN_BREAK, TOKEN_CONTINUE,
    // Web:
    TOKEN_API, TOKEN_MIDDLEWARE, TOKEN_GET, TOKEN_POST, TOKEN_PUT,
    TOKEN_DELETE, TOKEN_RPC, TOKEN_WEBSOCKET,
    TOKEN_PATCH, TOKEN_HEAD, TOKEN_OPTIONS,          // REST verb completeness (reserved → Phase 17)
    // Tooling:
    TOKEN_PARSE, TOKEN_JSON, TOKEN_CSV, TOKEN_XML, TOKEN_HTML,
    TOKEN_BUNDLE, TOKEN_MINIFY, TOKEN_CRYPTO, TOKEN_HASH, TOKEN_ENCRYPT,
    // Symmetry audit (P12 1A): crypto counterpart + base-encoding surface.
    TOKEN_DECRYPT,                                   // counterpart of ENCRYPT (reserved → Phase 13)
    TOKEN_ENCODE, TOKEN_DECODE,                      // base transform ops (functional in Phase 12)
    TOKEN_BASE64, TOKEN_BASE32, TOKEN_HEX,           // base namespaces
    TOKEN_SIGN, TOKEN_VERIFY,                         // signatures (reserved → Phase 13 crypto)
    TOKEN_SERIALIZE, TOKEN_STRINGIFY,                // (reserved → Phase 18, static per-type serializers)
    // Core:
    TOKEN_COMPTIME, TOKEN_SOA,
    TOKEN_UUID, TOKEN_GUID,                          // UUID/GUID primitive (Phase 13)

    TOKEN_STRING_LIT,       // common "text" or traditional """multiline"""
    TOKEN_STRING_INTERPOLATED,    // common `text {expr}` or interpolated ```multiline```
    TOKEN_STRING_RAW_LIT,         // $"text" or $"""multiline""" (ignores escapes)
    TOKEN_STRING_RAW_INTERP,       // $`text` or $```multiline``` (ignores escapes, processes interpolation)

    // Identifiers and Literals
    TOKEN_IDENTIFIER,
    TOKEN_MACRO_IDENT,       // e.g.: @marshall.to_ptr
    TOKEN_LIT_INT,           // e.g.: 1, 3_000, 255
    TOKEN_LIT_FLOAT,         // e.g.: 1.2
    TOKEN_LIT_STR,           // e.g.: "World"
    TOKEN_LIT_CHAR,          // e.g.: 'a'
    TOKEN_LIT_MULTILINE_STR, // e.g.: """...\n..."""

    // Punctuation Symbols and Delimiters
    TOKEN_LPAREN, TOKEN_RPAREN,     // ( )
    TOKEN_LBRACE, TOKEN_RBRACE,     // { }
    TOKEN_LBRACKET, TOKEN_RBRACKET, // [ ]
    TOKEN_SEMICOLON,                // ;
    TOKEN_COMMA,                    // ,
    TOKEN_DOT,                      // .
    TOKEN_QUESTION,                 // ?
    TOKEN_ELVIS,                    // ??
    TOKEN_SAFE_DOT,                 // ?.
    TOKEN_COLON,                    // :
    TOKEN_DBL_COLON,                // ::
    TOKEN_ARROW,                    // =>

    // Extended Mathematical, Logical, and Bitwise Operators
    TOKEN_PLUS,          // +
    TOKEN_MINUS,         // -
    TOKEN_MUL,           // *
    TOKEN_DIV,           // /
    TOKEN_MOD,           // %
    TOKEN_POW,           // **
    TOKEN_SHL,           // <<
    TOKEN_SHR,           // >>
    TOKEN_BIT_AND,       // &
    TOKEN_BIT_OR,        // |
    TOKEN_BIT_XOR,       // ^
    TOKEN_BIT_NOT,       // ~
    TOKEN_AND,           // &&
    TOKEN_OR,            // ||
    TOKEN_NOT,           // !
    TOKEN_LT,            // <
    TOKEN_LE,            // <=
    TOKEN_GT,            // >
    TOKEN_GE,            // >=
    TOKEN_EQ,            // ==
    TOKEN_NE,            // !=

    // Compound and Special Assignment Operators
    TOKEN_ASSIGN,        // =
    TOKEN_QUICK_ASSIGN,  // :=
    TOKEN_ADD_ASSIGN,    // +=
    TOKEN_SUB_ASSIGN,    // -=
    TOKEN_MUL_ASSIGN,    // *=
    TOKEN_DIV_ASSIGN,    // /=
    TOKEN_MOD_ASSIGN,    // %=
    TOKEN_SHL_ASSIGN,    // <<=
    TOKEN_SHR_ASSIGN,    // >>=
    TOKEN_AND_ASSIGN,    // &=
    TOKEN_OR_ASSIGN,     // |=
    TOKEN_XOR_ASSIGN     // ^=
} TokenType;

// Structure representing a Token
// Phase 12 — native literal unit suffixes, captured in the lexer at zero runtime
// cost. Time (ms/s/m/h/d), data (b/kb/mb/gb), and socket bandwidth (kbps/mbps/gbps).
// LIT_UNIT_NONE means the literal carried no unit suffix.
typedef enum {
    LIT_UNIT_NONE = 0,
    // Time
    LIT_UNIT_MS, LIT_UNIT_S, LIT_UNIT_M, LIT_UNIT_H, LIT_UNIT_D,
    // Data
    LIT_UNIT_B, LIT_UNIT_KB, LIT_UNIT_MB, LIT_UNIT_GB,
    // Bandwidth
    LIT_UNIT_KBPS, LIT_UNIT_MBPS, LIT_UNIT_GBPS
} LiteralUnit;

typedef struct {
    TokenType type;
    char* lexeme;
    int line;
    LiteralUnit literal_unit; // set for LIT_INT/LIT_FLOAT when a unit suffix is present
} Token;

// Lexer state
typedef struct {
    const char* source;
    int cursor;
    int line;
} Lexer;

// Public Lexer function signatures
void lexer_init(Lexer* lexer, const char* source);
Token lexer_next_token(Lexer* lexer);
const char* get_token_type_name(TokenType type);

#endif // LEXER_H