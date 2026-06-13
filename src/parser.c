#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Advances the parser's tokens
static void parser_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Initializes the parser by reading the first two tokens (lookahead)
void parser_init(Parser* parser, Lexer* lexer) {
    parser->lexer = lexer;
    // Load the initial lookahead
    parser->current_token = lexer_next_token(parser->lexer);
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Checks if the current token matches the expected type and advances, or reports a syntax error
static bool expect_token(Parser* parser, TokenType type, const char* context) {
    if (parser->current_token.type == type) {
        parser_advance(parser);
        return true;
    }
    fprintf(stderr, "[Syntax Error] Line %d: Expected '%s' in %s, but found '%s'\n",
            parser->current_token.line, get_token_type_name(type), context, parser->current_token.lexeme);
    return false;
}

// Parses a 'use' declaration while protecting the reserved namespace
// Supported syntax:
//   use my::namespace;
//   use dependency::other::app from "dependency";
ASTNode* parse_use_statement(Parser* parser) {
    if (!expect_token(parser, TOKEN_USE, "use declaration")) return NULL;

    ASTNode* node = (ASTNode*)malloc(sizeof(ASTNode));
    if (!node) return NULL;

    node->type = NODE_USE_STMT;
    node->data.use_stmt.path = NULL;
    node->data.use_stmt.from_source = NULL;

    char path_buffer[512] = {0};

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        // PROTECTION RULE: If the initial identifier is "teko" and this is not a stdlib compilation, block it!
        if (strcmp(parser->current_token.lexeme, "teko") == 0 && !parser->is_stdlib_compilation) {
            fprintf(stderr, "[Semantic Error] Line %d: The 'teko::' namespace is reserved for the system. Use the '@' prefix to access native libraries.\n",
                    parser->current_token.line);
            free(node);
            return NULL;
        }

        strcat(path_buffer, parser->current_token.lexeme);
        parser_advance(parser);
    } else {
        fprintf(stderr, "[Syntax Error] Line %d: Expected identifier after 'use'\n", parser->current_token.line);
        free(node);
        return NULL;
    }

    while (parser->current_token.type == TOKEN_DBL_COLON) {
        strcat(path_buffer, "::");
        parser_advance(parser);

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            strcat(path_buffer, parser->current_token.lexeme);
            parser_advance(parser);
        } else {
            fprintf(stderr, "[Syntax Error] Line %d: Expected identifier after '::'\n", parser->current_token.line);
            free(node);
            return NULL;
        }
    }

    node->data.use_stmt.path = strdup(path_buffer);

    if (parser->current_token.type == TOKEN_FROM) {
        parser_advance(parser);
        if (parser->current_token.type == TOKEN_LIT_STR) {
            node->data.use_stmt.from_source = strdup(parser->current_token.lexeme);
            parser_advance(parser);
        }
    }

    expect_token(parser, TOKEN_SEMICOLON, "end of use declaration");
    return node;
}

// Parser entry point for processing the file recursively
ASTNode* parser_parse_program(Parser* parser) {
    ASTNode* program_node = (ASTNode*)malloc(sizeof(ASTNode));
    program_node->type = NODE_PROGRAM;
    program_node->data.program.children = NULL;
    program_node->data.program.child_count = 0;

    int capacity = 4;
    program_node->data.program.children = (ASTNode**)malloc(sizeof(ASTNode*) * capacity);

    while (parser->current_token.type != TOKEN_EOF) {
        ASTNode* stmt = NULL;

        switch (parser->current_token.type) {
            case TOKEN_USE:
                stmt = parse_use_statement(parser);
                break;
            default:
                // Temporarily ignore unknown tokens at the global level to avoid loops during development
                parser_advance(parser);
                break;
        }

        if (stmt != NULL) {
            if (program_node->data.program.child_count >= capacity) {
                capacity *= 2;
                program_node->data.program.children = (ASTNode**)realloc(
                    program_node->data.program.children, sizeof(ASTNode*) * capacity);
            }
            program_node->data.program.children[program_node->data.program.child_count++] = stmt;
        }
    }

    return program_node;
}

// Clean recursive tree memory deallocation (prevents memory leaks)
void free_ast_node(ASTNode* node) {
    if (!node) return;

    if (node->type == NODE_USE_STMT) {
        free(node->data.use_stmt.path);
        if (node->data.use_stmt.from_source) {
            free(node->data.use_stmt.from_source);
        }
    } else if (node->type == NODE_PROGRAM) {
        for (int i = 0; i < node->data.program.child_count; i++) {
            free_ast_node(node->data.program.children[i]);
        }
        free(node->data.program.children);
    }
    free(node);
}