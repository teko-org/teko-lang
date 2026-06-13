#ifndef PARSER_STATEMENTS_H
#define PARSER_STATEMENTS_H

#include "parser.h"
#include "parser_types.h"

// Nodes for remaining declarations and statements in the AST
typedef enum {
    NODE_ELVIS_EXPR = 950,
    NODE_FUNC_DECL = 1000,
    NODE_VAR_DECL,
    NODE_FOR_LOOP,
    NODE_EXPR_STMT,
    NODE_BLOCK_STMT
} StatementASTNodeType;

// Structure representing local function parameters
typedef struct FuncParamNode {
    char* param_name;
    TypeInfo* param_type;
} FuncParamNode;

// Unified statement node for the AST
typedef struct StatementASTNode {
    int type; // StatementASTNodeType
    union {
        // Data for function declarations: [async] fn name(...) : type { ... }
        struct {
            char* fn_name;
            bool is_async;
            FuncParamNode* params;
            int param_count;
            TypeInfo* return_type;
            struct StatementASTNode** body_statements;
            int body_count;
        } func_decl;

        // Data for: let/mut name: type = expression;
        struct {
            char* var_name;
            bool is_mutable;
            TypeInfo* var_type; // May be NULL when type inference is used
            char* initializer_raw;
        } var_decl;

        // Data for: for (mut i: i32; i < len; i++) { ... }
        struct {
            struct StatementASTNode* init_stmt;
            char* condition_raw;
            char* increment_raw;
            struct StatementASTNode** body_statements;
            int body_count;
        } for_loop;

        // Data for standalone expressions (method calls, @ macros, simple assignments)
        struct {
            char* expression_raw;
        } expr_stmt;
    } data;
} StatementASTNode;

// Public signatures for the Declarations and Statements parser
StatementASTNode* parse_function_declaration(Parser* parser, bool is_async);
StatementASTNode* parse_variable_declaration(Parser* parser);
StatementASTNode* parse_for_loop(Parser* parser);
StatementASTNode* parse_statement(Parser* parser);
void free_statement_ast_node(StatementASTNode* node);

#endif // PARSER_STATEMENTS_H