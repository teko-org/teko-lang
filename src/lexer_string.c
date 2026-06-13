#include "lexer_string.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Local lexer pointer advancement
static char str_peek(Lexer* lexer) { return lexer->source[lexer->cursor]; }
static char str_peek_next(Lexer* lexer) {
    if (lexer->source[lexer->cursor] == '\0') return '\0';
    return lexer->source[lexer->cursor + 1];
}
static char str_advance(Lexer* lexer) {
    char c = lexer->source[lexer->cursor];
    if (c != '\0') lexer->cursor++;
    return c;
}

// Checks for a bX suffix (e.g.: b2, b3) at the end of the closing delimiter
static int parse_bracket_suffix(Lexer* lexer) {
    int arity = 1; // Default arity if omitted
    int original_cursor = lexer->cursor;

    if (str_peek(lexer) == 'b') {
        str_advance(lexer); // Consume 'b'
        if (isdigit((unsigned char)str_peek(lexer))) {
            char num_buf[16] = {0};
            int idx = 0;
            while (isdigit((unsigned char)str_peek(lexer)) && idx < 15) {
                num_buf[idx++] = str_advance(lexer);
            }
            arity = atoi(num_buf);
        } else {
            // If a bare 'b' was found without a number, revert the cursor
            lexer->cursor = original_cursor;
        }
    }
    return arity;
}

ExtendedStringToken lex_advanced_string(Lexer* lexer) {
    ExtendedStringToken token;
    token.type = TOKEN_STRING_LIT;
    token.bracket_arity = 1;
    token.is_multiline = false;
    token.is_raw = false;
    token.raw_content = NULL;

    int start_pos = lexer->cursor;

    // 1. Detect the RAW prefix ($)
    if (str_peek(lexer) == '$') {
        token.is_raw = true;
        str_advance(lexer); // Consume '$'
    }

    char delimiter = str_peek(lexer); // Can be '"' or '`'
    bool is_interpolated = (delimiter == '`');

    if (delimiter != '"' && delimiter != '`') {
        token.type = TOKEN_UNKNOWN;
        return token;
    }
    str_advance(lexer); // Consume the opening delimiter

    // 2. Detect multiline block (triple delimiter: """ or ```)
    if (str_peek(lexer) == delimiter && str_peek_next(lexer) == delimiter) {
        token.is_multiline = true;
        str_advance(lexer); // Consume the second
        str_advance(lexer); // Consume the third
    }

    int content_start = lexer->cursor;

    // 3. Scan the string body
    while (str_peek(lexer) != '\0') {
        char c = str_peek(lexer);

        if (c == '\n') {
            lexer->line++;
        }

        // Handle backslash escapes (\), but ONLY if this is not a RAW string ($)
        if (c == '\\' && !token.is_raw) {
            str_advance(lexer); // Consume '\\'
            if (str_peek(lexer) != '\0') {
                str_advance(lexer); // Consume the escaped character (e.g.: \n, \", \\)
            }
            continue;
        }

        // Check for block closing
        if (token.is_multiline) {
            if (c == delimiter && lexer->source[lexer->cursor + 1] == delimiter && lexer->source[lexer->cursor + 2] == delimiter) {
                int content_len = lexer->cursor - content_start;
                token.raw_content = (char*)malloc(content_len + 1);
                strncpy(token.raw_content, &lexer->source[content_start], content_len);
                token.raw_content[content_len] = '\0';

                str_advance(lexer); // Consume 1
                str_advance(lexer); // Consume 2
                str_advance(lexer); // Consume 3
                break;
            }
        } else {
            if (c == delimiter) {
                int content_len = lexer->cursor - content_start;
                token.raw_content = (char*)malloc(content_len + 1);
                strncpy(token.raw_content, &lexer->source[content_start], content_len);
                token.raw_content[content_len] = '\0';

                str_advance(lexer); // Consume the closing delimiter
                break;
            }
        }
        str_advance(lexer);
    }

    // 4. If interpolated, try to extract the optional 'bX' suffix (e.g.: b2) attached to the closing delimiter
    if (is_interpolated) {
        token.bracket_arity = parse_bracket_suffix(lexer);
        token.type = token.is_raw ? TOKEN_STRING_RAW_INTERP : TOKEN_STRING_INTERPOLATED;
    } else {
        token.type = token.is_raw ? TOKEN_STRING_RAW_LIT : TOKEN_STRING_LIT;
    }

    return token;
}

void free_extended_string_token(ExtendedStringToken* token) {
    if (token && token->raw_content) {
        free(token->raw_content);
        token->raw_content = NULL;
    }
}