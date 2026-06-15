#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Internal keyword mapping for fast lookup
typedef struct {
    const char* keyword;
    TokenType type;
} KeywordMap;

static KeywordMap keywords[] = {
    {"use", TOKEN_USE}, {"from", TOKEN_FROM}, {"extern", TOKEN_EXTERN},
    {"struct", TOKEN_STRUCT}, {"fn", TOKEN_FN}, {"as", TOKEN_AS},
    {"const", TOKEN_CONST}, {"async", TOKEN_ASYNC}, {"let", TOKEN_LET},
    {"mut", TOKEN_MUT}, {"defer", TOKEN_DEFER}, {"return", TOKEN_RETURN},
    {"when", TOKEN_WHEN}, {"for", TOKEN_FOR}, {"switch", TOKEN_SWITCH},
    {"null", TOKEN_NULL}, {"raised", TOKEN_RAISED}, {"where", TOKEN_WHERE},
    {"exp", TOKEN_EXP}, {"interface", TOKEN_INTERFACE}, {"service", TOKEN_SERVICE},
    {"decorates", TOKEN_DECORATES}, {"extend", TOKEN_EXTEND}, {"command", TOKEN_COMMAND},
    {"handler", TOKEN_HANDLER}, {"notification", TOKEN_NOTIFICATION}, {"with", TOKEN_WITH},
    {"query", TOKEN_QUERY}, {"operator", TOKEN_OPERATOR}, {"pub", TOKEN_PUB},
    {"required", TOKEN_REQD},
    // Phase 12 — reserved keyword matrix.
    // Resilience:
    {"circuit", TOKEN_CIRCUIT}, {"fallback", TOKEN_FALLBACK}, {"delayed", TOKEN_DELAYED},
    {"retry", TOKEN_RETRY}, {"exponential", TOKEN_EXPONENTIAL}, {"logarithmic", TOKEN_LOGARITHMIC},
    {"attempts", TOKEN_ATTEMPTS}, {"timeout", TOKEN_TIMEOUT},
    // OOP & concurrency:
    {"class", TOKEN_CLASS}, {"abstract", TOKEN_ABSTRACT}, {"trait", TOKEN_TRAIT},
    {"event", TOKEN_EVENT}, {"raise", TOKEN_RAISE}, {"subscribe", TOKEN_SUBSCRIBE},
    {"fanout", TOKEN_FANOUT}, {"fire_and_forget", TOKEN_FIRE_AND_FORGET}, {"shared", TOKEN_SHARED},
    {"atomic", TOKEN_ATOMIC}, {"routines", TOKEN_ROUTINES}, {"duplex", TOKEN_DUPLEX},
    // Web:
    {"api", TOKEN_API}, {"middleware", TOKEN_MIDDLEWARE}, {"get", TOKEN_GET},
    {"post", TOKEN_POST}, {"put", TOKEN_PUT}, {"delete", TOKEN_DELETE},
    {"rpc", TOKEN_RPC}, {"websocket", TOKEN_WEBSOCKET},
    {"patch", TOKEN_PATCH}, {"head", TOKEN_HEAD}, {"options", TOKEN_OPTIONS}, // reserved → Phase 17
    // Tooling:
    {"parse", TOKEN_PARSE}, {"json", TOKEN_JSON}, {"csv", TOKEN_CSV}, {"xml", TOKEN_XML},
    {"html", TOKEN_HTML}, {"bundle", TOKEN_BUNDLE}, {"minify", TOKEN_MINIFY},
    {"crypto", TOKEN_CRYPTO}, {"hash", TOKEN_HASH}, {"encrypt", TOKEN_ENCRYPT},
    // Symmetry audit (P12 1A): crypto counterpart + base-encoding surface.
    {"decrypt", TOKEN_DECRYPT},
    {"encode", TOKEN_ENCODE}, {"decode", TOKEN_DECODE},
    {"base64", TOKEN_BASE64}, {"base32", TOKEN_BASE32}, {"hex", TOKEN_HEX},
    {"sign", TOKEN_SIGN}, {"verify", TOKEN_VERIFY},                       // reserved → Phase 13
    {"serialize", TOKEN_SERIALIZE}, {"stringify", TOKEN_STRINGIFY},       // reserved → Phase 18
    // Core:
    {"comptime", TOKEN_COMPTIME}, {"soa", TOKEN_SOA},
    {"uuid", TOKEN_UUID}, {"guid", TOKEN_GUID},      // UUID/GUID primitive (Phase 13)
    {NULL, TOKEN_UNKNOWN}, // sentinel — MUST be last (the lookup loop stops here)
};

// Initializes the lexer
void lexer_init(Lexer* lexer, const char* source) {
    lexer->source = source;
    lexer->cursor = 0;
    lexer->line = 1;
}

// Private (static) helper functions for reading characters
static char peek(Lexer* lexer) {
    return lexer->source[lexer->cursor];
}

static char peek_next(Lexer* lexer) {
    if (lexer->source[lexer->cursor] == '\0') return '\0';
    return lexer->source[lexer->cursor + 1];
}

static char advance(Lexer* lexer) {
    char c = lexer->source[lexer->cursor];
    if (c != '\0') {
        lexer->cursor++;
    }
    return c;
}

static char* create_lexeme(const char* start, int length) {
    char* lexeme = (char*)malloc(length + 1);
    strncpy(lexeme, start, length);
    lexeme[length] = '\0';
    return lexeme;
}

static void skip_whitespace_and_comments(Lexer* lexer) {
    while (true) {
        char c = peek(lexer);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lexer);
        } else if (c == '\n') {
            lexer->line++;
            advance(lexer);
        } else if (c == '/' && peek_next(lexer) == '/') {
            while (peek(lexer) != '\n' && peek(lexer) != '\0') {
                advance(lexer);
            }
        } else {
            break;
        }
    }
}

static Token lex_string(Lexer* lexer) {
    const int start_pos = lexer->cursor;
    advance(lexer); // Consume the first '"'

    // Check if this is the start of a triple-quoted string: """
    if (peek(lexer) == '"' && peek_next(lexer) == '"') {
        advance(lexer); // Consume the second '"'
        advance(lexer); // Consume the third '"'

        // Scan until the closing """ is found
        while (true) {
            const char c = peek(lexer);
            if (c == '\0') break;

            if (c == '\n') {
                lexer->line++;
            }

            if (c == '"' && lexer->source[lexer->cursor + 1] == '"' && lexer->source[lexer->cursor + 2] == '"') {
                advance(lexer); // Consume the first '"'
                advance(lexer); // Consume the second '"'
                advance(lexer); // Consume the third '"'
                break;
            }
            advance(lexer);
        }

        const int length = lexer->cursor - start_pos;
        const Token token = {TOKEN_LIT_MULTILINE_STR, create_lexeme(&lexer->source[start_pos], length), lexer->line};
        return token;
    }

    // Default behavior for normal strings "..."
    while (peek(lexer) != '"' && peek(lexer) != '\0') {
        if (peek(lexer) == '\n') lexer->line++;
        advance(lexer);
    }
    if (peek(lexer) == '"') advance(lexer);

    const int length = lexer->cursor - start_pos;
    const Token token = {TOKEN_LIT_STR, create_lexeme(&lexer->source[start_pos], length), lexer->line};
    return token;
}

static Token lex_char(Lexer* lexer) {
    const int start_pos = lexer->cursor;
    advance(lexer); // consume '\''

    if (peek(lexer) == '\\') advance(lexer);
    advance(lexer);

    if (peek(lexer) == '\'') advance(lexer);

    int length = lexer->cursor - start_pos;
    Token token = {TOKEN_LIT_CHAR, create_lexeme(&lexer->source[start_pos], length), lexer->line};
    return token;
}

// Phase 12 — native literal unit suffixes. The whole trailing [a-z]+ run after a
// number is matched as one unit (so the match is longest by construction: "ms" wins
// over "m", "kbps" over "kb"); a run that matches no unit is NOT consumed (rewound),
// leaving it to be lexed as a separate identifier — preserving prior behavior.
static const struct { const char* s; LiteralUnit u; } literal_units[] = {
    {"ms", LIT_UNIT_MS}, {"s", LIT_UNIT_S}, {"m", LIT_UNIT_M}, {"h", LIT_UNIT_H}, {"d", LIT_UNIT_D},
    {"b", LIT_UNIT_B}, {"kb", LIT_UNIT_KB}, {"mb", LIT_UNIT_MB}, {"gb", LIT_UNIT_GB},
    {"kbps", LIT_UNIT_KBPS}, {"mbps", LIT_UNIT_MBPS}, {"gbps", LIT_UNIT_GBPS},
    {NULL, LIT_UNIT_NONE}
};

static Token lex_number(Lexer* lexer) {
    int start_pos = lexer->cursor;
    bool is_float = false;

    while (isdigit(peek(lexer)) || peek(lexer) == '_') advance(lexer);

    if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
        is_float = true;
        advance(lexer);
        while (isdigit(peek(lexer)) || peek(lexer) == '_') advance(lexer);
    }

    int length = lexer->cursor - start_pos; // numeric text length (excludes any suffix)

    // Optional unit suffix: scan the maximal trailing alpha run and match it whole.
    LiteralUnit unit = LIT_UNIT_NONE;
    int suffix_start = lexer->cursor;
    while (isalpha(peek(lexer))) advance(lexer);
    int suffix_len = lexer->cursor - suffix_start;
    if (suffix_len > 0) {
        char suffix[8];
        if (suffix_len < (int)sizeof(suffix)) {
            memcpy(suffix, &lexer->source[suffix_start], suffix_len);
            suffix[suffix_len] = '\0';
            for (int i = 0; literal_units[i].s != NULL; i++) {
                if (strcmp(suffix, literal_units[i].s) == 0) { unit = literal_units[i].u; break; }
            }
        }
        if (unit == LIT_UNIT_NONE) lexer->cursor = suffix_start; // not a unit — rewind
    }

    Token token = {is_float ? TOKEN_LIT_FLOAT : TOKEN_LIT_INT,
                   create_lexeme(&lexer->source[start_pos], length), lexer->line, unit};
    return token;
}

static Token lex_identifier(Lexer* lexer, bool is_macro) {
    int start_pos = lexer->cursor;

    if (is_macro) advance(lexer); // consume '@'

    while (isalnum(peek(lexer)) || peek(lexer) == '_' || peek(lexer) == '.') {
        if (peek(lexer) == '.' && !isalnum(peek_next(lexer)) && peek_next(lexer) != '_') {
            break;
        }
        advance(lexer);
    }

    int length = lexer->cursor - start_pos;
    Token token;
    token.lexeme = create_lexeme(&lexer->source[start_pos], length);
    token.line = lexer->line;
    token.literal_unit = LIT_UNIT_NONE;

    if (is_macro) {
        token.type = TOKEN_MACRO_IDENT;
    } else {
        token.type = TOKEN_IDENTIFIER;
        for (int i = 0; keywords[i].keyword != NULL; i++) {
            if (strcmp(token.lexeme, keywords[i].keyword) == 0) {
                token.type = keywords[i].type;
                break;
            }
        }
    }
    return token;
}

// Returns the next Token read from the source file
Token lexer_next_token(Lexer* lexer) {
    skip_whitespace_and_comments(lexer);

    char c = peek(lexer);

    if (c == '\0') {
        Token token = {TOKEN_EOF, strdup("EOF"), lexer->line};
        return token;
    }

    if (c == '"') return lex_string(lexer);
    if (c == '\'') return lex_char(lexer);
    if (isdigit(c)) return lex_number(lexer);
    if (isalpha(c) || c == '_') return lex_identifier(lexer, false);
    if (c == '@') return lex_identifier(lexer, true);

    int start_pos = lexer->cursor;
    advance(lexer);

    TokenType type;

    switch (c) {
        case '(': type = TOKEN_LPAREN; break;
        case ')': type = TOKEN_RPAREN; break;
        case '{': type = TOKEN_LBRACE; break;
        case '}': type = TOKEN_RBRACE; break;
        case '[': type = TOKEN_LBRACKET; break;
        case ']': type = TOKEN_RBRACKET; break;
        case ';': type = TOKEN_SEMICOLON; break;
        case ',': type = TOKEN_COMMA; break;
        case '?':
            if (peek(lexer) == '?') {
                advance(lexer);
                type = TOKEN_ELVIS;
            } else if (peek(lexer) == '.') {
                advance(lexer);
                type = TOKEN_SAFE_DOT; // Captures ?. as a single token
            } else {
                type = TOKEN_QUESTION;
            }
            break;
        case '.': type = TOKEN_DOT; break;

        case '+':
            if (peek(lexer) == '=') { advance(lexer); type = TOKEN_ADD_ASSIGN; }
            else { type = TOKEN_PLUS; }
            break;
        case '-':
            if (peek(lexer) == '=') { advance(lexer); type = TOKEN_SUB_ASSIGN; }
            else { type = TOKEN_MINUS; }
            break;
        case '*':
            if (peek(lexer) == '*') { advance(lexer); type = TOKEN_POW; }
            else if (peek(lexer) == '=') { advance(lexer); type = TOKEN_MUL_ASSIGN; }
            else { type = TOKEN_MUL; }
            break;
        case '/':
            if (peek(lexer) == '=') { advance(lexer); type = TOKEN_DIV_ASSIGN; }
            else { type = TOKEN_DIV; }
            break;
        case '%':
            if (peek(lexer) == '=') { advance(lexer); type = TOKEN_MOD_ASSIGN; }
            else { type = TOKEN_MOD; }
            break;
        case '~':
            type = TOKEN_BIT_NOT;
            break;
        case '^':
            if (peek(lexer) == '=') { advance(lexer); type = TOKEN_XOR_ASSIGN; }
            else { type = TOKEN_BIT_XOR; }
            break;
        case '&':
            if (peek(lexer) == '&') { advance(lexer); type = TOKEN_AND; }
            else if (peek(lexer) == '=') { advance(lexer); type = TOKEN_AND_ASSIGN; }
            else { type = TOKEN_BIT_AND; }
            break;
        case '|':
            if (peek(lexer) == '|') { advance(lexer); type = TOKEN_OR; }
            else if (peek(lexer) == '=') { advance(lexer); type = TOKEN_OR_ASSIGN; }
            else { type = TOKEN_BIT_OR; }
            break;
        case '!':
            if (peek(lexer) == '=') { advance(lexer); type = TOKEN_NE; }
            else { type = TOKEN_NOT; }
            break;
        case '<':
            if (peek(lexer) == '<') {
                advance(lexer);
                if (peek(lexer) == '=') { advance(lexer); type = TOKEN_SHL_ASSIGN; }
                else { type = TOKEN_SHL; }
            } else if (peek(lexer) == '=') {
                advance(lexer);
                type = TOKEN_LE;
            } else {
                type = TOKEN_LT;
            }
            break;
        case '>':
            if (peek(lexer) == '>') {
                advance(lexer);
                if (peek(lexer) == '=') { advance(lexer); type = TOKEN_SHR_ASSIGN; }
                else { type = TOKEN_SHR; }
            } else if (peek(lexer) == '=') {
                advance(lexer);
                type = TOKEN_GE;
            } else {
                type = TOKEN_GT;
            }
            break;
        case '=':
            if (peek(lexer) == '>') { advance(lexer); type = TOKEN_ARROW; }
            else if (peek(lexer) == '=') { advance(lexer); type = TOKEN_EQ; }
            else { type = TOKEN_ASSIGN; }
            break;
        case ':':
            if (peek(lexer) == ':') { advance(lexer); type = TOKEN_DBL_COLON; }
            else if (peek(lexer) == '=') { advance(lexer); type = TOKEN_QUICK_ASSIGN; }
            else { type = TOKEN_COLON; }
            break;

        default:
            type = TOKEN_UNKNOWN;
            break;
    }

    Token token = {type, create_lexeme(&lexer->source[start_pos], lexer->cursor - start_pos), lexer->line};
    return token;
}

const char* get_token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_EOF: return "EOF";
        case TOKEN_UNKNOWN: return "UNKNOWN_OR_ERROR";

        // Keywords
        case TOKEN_USE: return "KEYWORD_USE";
        case TOKEN_FROM: return "KEYWORD_FROM";
        case TOKEN_EXTERN: return "KEYWORD_EXTERN";
        case TOKEN_STRUCT: return "KEYWORD_STRUCT";
        case TOKEN_FN: return "KEYWORD_FN";
        case TOKEN_AS: return "KEYWORD_AS";
        case TOKEN_CONST: return "KEYWORD_CONST";
        case TOKEN_ASYNC: return "KEYWORD_ASYNC";
        case TOKEN_LET: return "KEYWORD_LET";
        case TOKEN_MUT: return "KEYWORD_MUT";
        case TOKEN_DEFER: return "KEYWORD_DEFER";
        case TOKEN_RETURN: return "KEYWORD_RETURN";
        case TOKEN_WHEN: return "KEYWORD_WHEN";
        case TOKEN_FOR: return "KEYWORD_FOR";
        case TOKEN_SWITCH: return "KEYWORD_SWITCH";
        case TOKEN_NULL: return "KEYWORD_NULL";
        case TOKEN_RAISED: return "KEYWORD_RAISED";
        case TOKEN_WHERE: return "KEYWORD_WHERE";
        case TOKEN_EXP: return "KEYWORD_EXP";
        case TOKEN_INTERFACE: return "KEYWORD_INTERFACE";
        case TOKEN_SERVICE: return "KEYWORD_SERVICE";
        case TOKEN_DECORATES: return "KEYWORD_DECORATES";
        case TOKEN_EXTEND: return "KEYWORD_EXTEND";
        case TOKEN_COMMAND: return "KEYWORD_COMMAND";
        case TOKEN_HANDLER: return "KEYWORD_HANDLER";
        case TOKEN_NOTIFICATION: return "KEYWORD_NOTIFICATION";
        case TOKEN_WITH: return "KEYWORD_WITH";
        case TOKEN_QUERY: return "KEYWORD_QUERY";
        case TOKEN_OPERATOR: return "KEYWORD_OPERATOR";
        case TOKEN_PUB: return "KEYWORD_PUB";
        case TOKEN_REQD: return "KEYWORD_REQUIRED";

        // Identifiers and Literals
        case TOKEN_IDENTIFIER: return "IDENTIFIER";
        case TOKEN_MACRO_IDENT: return "MACRO_IDENTIFIER";
        case TOKEN_LIT_INT: return "LITERAL_INT";
        case TOKEN_LIT_FLOAT: return "LITERAL_FLOAT";
        case TOKEN_LIT_STR: return "LITERAL_STRING";
        case TOKEN_LIT_CHAR: return "LITERAL_CHAR";
        case TOKEN_LIT_MULTILINE_STR: return "LITERAL_MULTILINE_STRING";

        case TOKEN_STRING_LIT: return "STRING_LITERAL";
        case TOKEN_STRING_INTERPOLATED: return "STRING_INTERPOLATED";
        case TOKEN_STRING_RAW_LIT: return "STRING_RAW_LITERAL";
        case TOKEN_STRING_RAW_INTERP: return "STRING_RAW_INTERPOLATED";
        case TOKEN_SAFE_DOT: return "SAFE_NAVIGATION_DOT";
        case TOKEN_ELVIS: return "OPERATOR_ELVIS";

        // Delimiters and Punctuation
        case TOKEN_LPAREN: return "LPAREN";
        case TOKEN_RPAREN: return "RPAREN";
        case TOKEN_LBRACE: return "LBRACE";
        case TOKEN_RBRACE: return "RBRACE";
        case TOKEN_LBRACKET: return "LBRACKET";
        case TOKEN_RBRACKET: return "RBRACKET";
        case TOKEN_SEMICOLON: return "SEMICOLON";
        case TOKEN_COMMA: return "COMMA";
        case TOKEN_DOT: return "DOT";
        case TOKEN_QUESTION: return "QUESTION";
        case TOKEN_COLON: return "COLON";
        case TOKEN_DBL_COLON: return "DOUBLE_COLON";
        case TOKEN_ARROW: return "ARROW_INLINE";

        // Extended Mathematical, Logical, and Bitwise Operators
        case TOKEN_PLUS: return "OPERATOR_PLUS";
        case TOKEN_MINUS: return "OPERATOR_MINUS";
        case TOKEN_MUL: return "OPERATOR_MULTIPLY";
        case TOKEN_DIV: return "OPERATOR_DIVIDE";
        case TOKEN_MOD: return "OPERATOR_MODULO";
        case TOKEN_POW: return "OPERATOR_POWER";
        case TOKEN_SHL: return "OPERATOR_SHIFT_LEFT";
        case TOKEN_SHR: return "OPERATOR_SHIFT_RIGHT";
        case TOKEN_BIT_AND: return "OPERATOR_BITWISE_AND";
        case TOKEN_BIT_OR: return "OPERATOR_BITWISE_OR";
        case TOKEN_BIT_XOR: return "OPERATOR_BITWISE_XOR";
        case TOKEN_BIT_NOT: return "OPERATOR_BITWISE_NOT";
        case TOKEN_AND: return "OPERATOR_LOGICAL_AND";
        case TOKEN_OR: return "OPERATOR_LOGICAL_OR";
        case TOKEN_NOT: return "OPERATOR_LOGICAL_NOT";
        case TOKEN_LT: return "OPERATOR_LESS_THAN";
        case TOKEN_LE: return "OPERATOR_LESS_EQUAL";
        case TOKEN_GT: return "OPERATOR_GREATER_THAN";
        case TOKEN_GE: return "OPERATOR_GREATER_EQUAL";
        case TOKEN_EQ: return "OPERATOR_EQUAL_COMPARISON";
        case TOKEN_NE: return "OPERATOR_NOT_EQUAL";

        // Compound and Special Assignment Operators
        case TOKEN_ASSIGN: return "ASSIGN_EQUAL";
        case TOKEN_QUICK_ASSIGN: return "QUICK_ASSIGN";
        case TOKEN_ADD_ASSIGN: return "ASSIGN_ADD";
        case TOKEN_SUB_ASSIGN: return "ASSIGN_SUB";
        case TOKEN_MUL_ASSIGN: return "ASSIGN_MUL";
        case TOKEN_DIV_ASSIGN: return "ASSIGN_DIV";
        case TOKEN_MOD_ASSIGN: return "ASSIGN_MOD";
        case TOKEN_SHL_ASSIGN: return "ASSIGN_SHIFT_LEFT";
        case TOKEN_SHR_ASSIGN: return "ASSIGN_SHIFT_RIGHT";
        case TOKEN_AND_ASSIGN: return "ASSIGN_BITWISE_AND";
        case TOKEN_OR_ASSIGN: return "ASSIGN_BITWISE_OR";
        case TOKEN_XOR_ASSIGN: return "ASSIGN_BITWISE_XOR";

        default: return "UNKNOWN_OR_ERROR";
    }
}