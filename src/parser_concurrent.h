#ifndef PARSER_CONCURRENT_H
#define PARSER_CONCURRENT_H

#include "parser.h"

// Specific nodes for concurrency in the AST
typedef enum {
    NODE_ASYNC_FN = 200,   // Start of an async block
    NODE_AWAIT_EXPR,       // await expression
    NODE_DEFER_BLOCK,      // Regular defer block
    NODE_ASYNC_DEFER_BLOCK,// Async defer block
    NODE_WHEN_MODIFIER,    // Post-fix conditional command 'when'
    NODE_CHAN_INIT,        // Channel initialization: chan<T> or chan<T>(cap)
    NODE_CONCURRENT_OBJECT // waiter or mutex
} ConcurrentASTNodeType;

typedef enum {
    OBJ_WAITER,
    OBJ_MUTEX
} ConcurrentObjectType;

// Unified concurrent node structure for the AST
typedef struct ConcurrentASTNode {
    int type; // ConcurrentASTNodeType
    union {
        // await expression: await <expr>
        struct {
            struct ConcurrentASTNode* expression;
        } await_expr;

        // Post-fix conditional modifier: <cmd> when <condition>
        struct {
            struct ConcurrentASTNode* command;
            char* condition_lexeme;
        } when_modifier;

        // Channel initialization: chan<type>(capacity)
        struct {
            char* channel_type;
            int capacity; // 0 if unbounded (no capacity defined)
        } chan_init;

        // Synchronization primitives: waiter or mutex
        struct {
            ConcurrentObjectType obj_type;
        } sync_obj;

        // defer / async defer blocks
        struct {
            struct ConcurrentASTNode** statements;
            int statement_count;
        } defer_block;
    } data;
} ConcurrentASTNode;

// Function signatures for the Concurrency parser
ConcurrentASTNode* parse_await_expression(Parser* parser);
ConcurrentASTNode* parse_defer_statement(Parser* parser, bool is_async);
ConcurrentASTNode* parse_concurrency_assignment(Parser* parser);
void free_concurrent_ast_node(ConcurrentASTNode* node);

#endif // PARSER_CONCURRENT_H