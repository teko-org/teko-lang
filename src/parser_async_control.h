#ifndef PARSER_ASYNC_CONTROL_H
#define PARSER_ASYNC_CONTROL_H

#include "parser.h"

// Specific nodes for async control flow and branching in the AST
typedef enum {
    NODE_WHEN_EXPR = 900,
    NODE_INLINE_SWITCH,
    NODE_STATEMENT_SWITCH,
    NODE_SWITCH_ARM,
    NODE_RAISED_CATCH
} AsyncControlASTNodeType;

// Structure for inline switch arms (e.g.: null => 1;)
typedef struct SwitchArmNode {
    char* pattern_lexeme;       // E.g.: "null", "_"
    char* condition_raw;        // Content after 'when' if present (e.g.: "exs.name == \"World\"")
    struct AsyncControlASTNode* value_expr; // Resulting expression (may contain await)
} SwitchArmNode;

// Structure for standard switch case blocks (e.g.: null => { ... })
typedef struct SwitchCaseNode {
    char* condition_pattern;      // E.g.: "null", "_", or literal values
    char* case_body_raw;          // Statements inside the case
} SwitchCaseNode;

// Main async control node for the AST
typedef struct AsyncControlASTNode {
    int type; // AsyncControlASTNodeType
    union {
        // Data for post-fix modifier: <cmd> when <cond>
        struct {
            char* command_raw;
            char* condition_raw;
        } when_expr;

        // Data for inline switch: expression switch { ... }
        struct {
            char* target_expression_raw;
            SwitchArmNode* arms;
            int arm_count;
        } inline_switch;

        // Data for error interceptor: ... raised (err) { ... }
        struct {
            struct AsyncControlASTNode* protected_block;
            char* error_variable_name; // E.g.: "err"
            char* catch_body_raw;
        } raised_catch;

        // Data for standard/common switch
        struct {
            char* control_expression_raw; // Evaluated expression, e.g.: "(exs)"
            SwitchCaseNode* cases;
            int case_count;
        } statement_switch;
    } data;
} AsyncControlASTNode;

// Public signatures for the Async Control parser
AsyncControlASTNode* parse_async_control_statement(Parser* parser);
void free_async_control_ast_node(AsyncControlASTNode* node);

#endif // PARSER_ASYNC_CONTROL_H