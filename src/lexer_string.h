#ifndef LEXER_STRING_H
#define LEXER_STRING_H

#include "lexer.h"

// Structure that extends token metadata to support the parser in destructuring interpolations
typedef struct ExtendedStringToken {
    int type;                // StringTokenType
    char* raw_content;       // Full textual content extracted from the string
    int bracket_arity;       // The 'X' quantity of the bX suffix (default: 1)
    bool is_multiline;       // Indicates whether triple quotes/backticks were used
    bool is_raw;             // Indicates whether the '$' prefix was used
} ExtendedStringToken;

// Signatures to hook into the scanning engine
ExtendedStringToken lex_advanced_string(Lexer* lexer);
void free_extended_string_token(ExtendedStringToken* token);

#endif // LEXER_STRING_H