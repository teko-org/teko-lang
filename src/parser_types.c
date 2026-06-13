#include "parser_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void types_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Initializes a clean TypeInfo structure
static TypeInfo* create_empty_type_info() {
    const auto info = (TypeInfo*)malloc(sizeof(TypeInfo));
    info->kind = NODE_TYPE_BASIC;
    info->base_name = NULL;
    info->is_nullable = false;
    info->is_array = false;
    info->is_array_elem_mut = false;
    info->generic_params = NULL;
    info->generic_param_count = 0;
    return info;
}

// Recursively parses any language type or identifier, expanding the @ prefix
TypeInfo* parse_complete_type_info(Parser* parser) {
    TypeInfo* info = create_empty_type_info();
    if (!info) return NULL;

    // 1. Check for 'mut' modifier on arrays
    if (parser->current_token.type == TOKEN_MUT) {
        info->is_array_elem_mut = true;
        types_advance(parser);
    }

    // 2. Capture and expand the base name if it is an @ macro/virtual namespace
    if (parser->current_token.type == TOKEN_MACRO_IDENT) {
        // If it starts with '@', extract the fragment after '@' and prepend 'teko::'
        const char* raw_lexeme = parser->current_token.lexeme;

        if (raw_lexeme[0] == '@') {
            // Calculate space: "teko::" (6 chars) + rest of lexeme (strlen - 1) + null terminator (1)
            int expanded_len = 6 + (int)strlen(raw_lexeme) - 1;
            info->base_name = (char*)malloc(expanded_len + 1);
            if (info->base_name) {
                strcpy(info->base_name, "teko::");
                strcat(info->base_name, &raw_lexeme[1]); // Skip the '@' character
            }
        } else {
            info->base_name = strdup(raw_lexeme);
        }

        info->kind = NODE_TYPE_GENERIC; // Treated as a qualified macro/type path
        types_advance(parser);
    }
    else if (parser->current_token.type == TOKEN_IDENTIFIER) {
        info->base_name = strdup(parser->current_token.lexeme);

        if (strcmp(info->base_name, "decimal") == 0) info->kind = NODE_TYPE_DECIMAL;
        else if (strcmp(info->base_name, "bigint") == 0) info->kind = NODE_TYPE_BIGINT;
        else if (strcmp(info->base_name, "arena") == 0) info->kind = NODE_TYPE_ARENA;
        else if (strcmp(info->base_name, "func") == 0) info->kind = NODE_TYPE_FUNC;

        types_advance(parser);
    } else {
        info->base_name = strdup("void");
    }

    // 3. Process generic parameters <T, U>
    if (parser->current_token.type == TOKEN_LT) {
        if (info->kind != NODE_TYPE_FUNC) {
            info->kind = NODE_TYPE_GENERIC;
        }
        types_advance(parser);

        int cap = 4;
        info->generic_params = (TypeInfo**)malloc(sizeof(TypeInfo*) * cap);

        while (parser->current_token.type != TOKEN_GT && parser->current_token.type != TOKEN_EOF) {
            if (info->generic_param_count >= cap) {
                cap *= 2;
                info->generic_params = (TypeInfo**)realloc(info->generic_params, sizeof(TypeInfo*) * cap);
            }

            info->generic_params[info->generic_param_count++] = parse_complete_type_info(parser);

            if (parser->current_token.type == TOKEN_COMMA) {
                types_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_GT) {
            types_advance(parser);
        }
    }

    // 4. Handle nullability
    if (parser->current_token.type == TOKEN_QUESTION) {
        info->is_nullable = true;
        types_advance(parser);
    }

    // 5. Handle arrays
    if (parser->current_token.type == TOKEN_LBRACKET) {
        types_advance(parser); // Consume '['

        // If a mode identifier is present inside (e.g.: 'a')
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            // Store the mode (e.g.: "a") by reusing a string in the structure if needed,
            // or simply advance to validate the syntax for the current scope.
            types_advance(parser);
        }

        if (parser->current_token.type == TOKEN_RBRACKET) {
            info->is_array = true;
            types_advance(parser); // Consume ']'
        } else {
            fprintf(stderr, "[Syntax Error] Line %d: Expected ']' to close the type descriptor.\n", parser->current_token.line);
        }
    }

    return info;
}

// Parses standalone numeric literals
ArbitraryTypeASTNode* parse_arbitrary_numeric_literal(Parser* parser) {
    const auto node = (ArbitraryTypeASTNode*)malloc(sizeof(ArbitraryTypeASTNode));
    node->type = NODE_ARBITRARY_LITERAL;
    node->data.numeric_literal.raw_lexeme = NULL;
    node->data.numeric_literal.is_floating_point = false;

    if (parser->current_token.type == TOKEN_LIT_INT || parser->current_token.type == TOKEN_LIT_FLOAT) {
        node->data.numeric_literal.raw_lexeme = strdup(parser->current_token.lexeme);
        node->data.numeric_literal.is_floating_point = (parser->current_token.type == TOKEN_LIT_FLOAT);
        types_advance(parser);
    }
    return node;
}

// Safe recursive deallocation of TypeInfo
void free_type_info(TypeInfo* info) {
    if (!info) return;
    if (info->base_name) free(info->base_name);
    if (info->generic_params) {
        for (int i = 0; i < info->generic_param_count; i++) {
            free_type_info(info->generic_params[i]);
        }
        free(info->generic_params);
    }
    free(info);
}

void free_arbitrary_type_ast_node(ArbitraryTypeASTNode* node) {
    if (!node) return;

    // Exact check against the enum symbol
    if (node->type == NODE_ARBITRARY_LITERAL) {
        if (node->data.numeric_literal.raw_lexeme) {
            free(node->data.numeric_literal.raw_lexeme);
        }
    } else {
        if (node->data.type_decl.type_info) {
            free_type_info(node->data.type_decl.type_info);
        }
        if (node->data.type_decl.constraint_target) {
            free(node->data.type_decl.constraint_target);
        }
        if (node->data.type_decl.constraint_bound) {
            free(node->data.type_decl.constraint_bound);
        }
    }
    free(node);
}
