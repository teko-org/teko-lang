#ifndef PARSER_STRING_H
#define PARSER_STRING_H

#include "parser.h"
#include "lexer_string.h"

// Specific nodes for the advanced string engine in the AST
typedef enum {
    NODE_STRING_LITERAL_EXPR = 610, // Pure static string
    NODE_STRING_INTERPOLATED_EXPR,  // Interpolated string containing text and logic blocks
    NODE_STRING_PART_STATIC,        // Static part of the interpolation
    NODE_STRING_PART_EXPRESSION     // Internal expression (e.g.: variable or constant)
} StringASTNodeType;

// Represents a fragment of an interpolated string
typedef struct StringPartNode {
    int type; // NODE_STRING_PART_STATIC ou NODE_STRING_PART_EXPRESSION
    char* content; // Raw static text or the lexeme of the expression to be resolved
} StringPartNode;

// Main string node in the AST
typedef struct StringASTNode {
    int type; // StringASTNodeType
    bool is_raw;
    bool is_multiline;
    union {
        // For pure static string literals (normal or raw)
        struct {
            char* value;
        } static_string;

        // For destructured interpolated strings
        struct {
            StringPartNode* parts;
            int part_count;
            int bracket_arity; // Arity 'X' of the bX suffix
        } interpolated_string;
    } data;
} StringASTNode;

// Public signatures for the String parser
StringASTNode* parse_advanced_string_expr(Parser* parser, ExtendedStringToken* ext_token);
void free_string_ast_node(StringASTNode* node);

#endif // PARSER_STRING_H