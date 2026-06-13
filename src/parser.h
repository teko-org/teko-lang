#ifndef PARSER_H
#define PARSER_H

#include "lexer.h"

// AST node type
typedef enum {
    NODE_PROGRAM,
    NODE_USE_STMT,
    /* Add new nodes here as the parser expands */
} ASTNodeType;

// Generic structure for AST nodes
typedef struct ASTNode {
    ASTNodeType type;
    union {
        // Data for the 'use' node (e.g. use my::namespace; or use dep from "str")
        struct {
            char* path;
            char* from_source; // Populated when 'from "..."' is present
        } use_stmt;

        // Program block containing multiple child nodes
        struct {
            struct ASTNode** children;
            int child_count;
        } program;
    } data;
} ASTNode;

// Parser state
typedef struct {
    Lexer* lexer;
    Token current_token;
    Token peek_token;
    bool is_stdlib_compilation;
} Parser;

// Public parser functions
void parser_init(Parser* parser, Lexer* lexer);
ASTNode* parser_parse_program(Parser* parser);
ASTNode* parse_use_statement(Parser* parser);
void free_ast_node(ASTNode* node);

#endif // PARSER_H