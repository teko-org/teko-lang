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
typedef struct {
    TokenType type;
    char* lexeme;
    int line;
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