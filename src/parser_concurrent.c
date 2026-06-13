#include "parser_concurrent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void conc_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Parses 'await ...' expressions
ConcurrentASTNode* parse_await_expression(Parser* parser) {
    conc_advance(parser); // Consume 'await'

    ConcurrentASTNode* node = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
    node->type = NODE_AWAIT_EXPR;
    node->data.await_expr.expression = NULL;

    // In a full implementation this would call the generic expression parse function.
    // Here we capture the async call identifier to compose the AST.
    if (parser->current_token.type == TOKEN_IDENTIFIER || parser->current_token.type == TOKEN_MACRO_IDENT) {
        ConcurrentASTNode* expr_child = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
        // Simplified as a leaf identifier for this scope
        expr_child->type = NODE_CONCURRENT_OBJECT;
        node->data.await_expr.expression = expr_child;
        conc_advance(parser);
    }

    return node;
}

// Parses 'defer' and 'async defer' blocks
ConcurrentASTNode* parse_defer_statement(Parser* parser, bool is_async) {
    conc_advance(parser); // Consume 'defer'

    ConcurrentASTNode* node = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
    node->type = is_async ? NODE_ASYNC_DEFER_BLOCK : NODE_DEFER_BLOCK;
    node->data.defer_block.statements = NULL;
    node->data.defer_block.statement_count = 0;

    if (parser->current_token.type == TOKEN_LBRACE) {
        conc_advance(parser); // Consume '{'

        int cap = 4;
        node->data.defer_block.statements = (ConcurrentASTNode**)malloc(sizeof(ConcurrentASTNode*) * cap);

        // Scan the inner scope of the defer block
        while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                // Simple advance to simulate consuming internal commands in the example
                conc_advance(parser);
            } else {
                conc_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_RBRACE) {
            conc_advance(parser); // Consume '}'
        }
    }

    return node;
}

// Parses quick ':=' operator assignments (channels, waiter, mutex)
ConcurrentASTNode* parse_concurrency_assignment(Parser* parser) {
    ConcurrentASTNode* node = (ConcurrentASTNode*)malloc(sizeof(ConcurrentASTNode));
    node->type = TOKEN_UNKNOWN;

    // Case 1: Channel initialization -> ch := chan<i32> or chan<i32>(1)
    if (strcmp(parser->current_token.lexeme, "chan") == 0) {
        node->type = NODE_CHAN_INIT;
        node->data.chan_init.channel_type = NULL;
        node->data.chan_init.capacity = 0; // Unbounded by default

        conc_advance(parser); // Consume 'chan'

        if (parser->current_token.type == TOKEN_LT) {
            conc_advance(parser); // Consume '<'
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                node->data.chan_init.channel_type = strdup(parser->current_token.lexeme);
                conc_advance(parser);
            }
            if (parser->current_token.type == TOKEN_GT) {
                conc_advance(parser); // Consume '>'
            }
        }

        // Check if the channel is bounded (e.g.: chan<i32>(1))
        if (parser->current_token.type == TOKEN_LPAREN) {
            conc_advance(parser); // Consume '('
            if (parser->current_token.type == TOKEN_LIT_INT) {
                node->data.chan_init.capacity = atoi(parser->current_token.lexeme);
                conc_advance(parser);
            }
            if (parser->current_token.type == TOKEN_RPAREN) {
                conc_advance(parser); // Consume ')'
            }
        }
    }
    // Case 2: Semaphores and wait groups -> wg := waiter
    else if (strcmp(parser->current_token.lexeme, "waiter") == 0) {
        node->type = NODE_CONCURRENT_OBJECT;
        node->data.sync_obj.obj_type = OBJ_WAITER;
        conc_advance(parser);
    }
    // Case 3: Mutual exclusion -> mtx := mutex
    else if (strcmp(parser->current_token.lexeme, "mutex") == 0) {
        node->type = NODE_CONCURRENT_OBJECT;
        node->data.sync_obj.obj_type = OBJ_MUTEX;
        conc_advance(parser);
    }

    return node;
}

// Safe memory cleanup avoiding null pointer dereferences on parse failures
void free_concurrent_ast_node(ConcurrentASTNode* node) {
    if (!node) return;

    if (node->type == NODE_AWAIT_EXPR) {
        if (node->data.await_expr.expression) {
            free_concurrent_ast_node(node->data.await_expr.expression);
        }
    } else if (node->type == NODE_WHEN_MODIFIER) {
        if (node->data.when_modifier.command) {
            free_concurrent_ast_node(node->data.when_modifier.command);
        }
        if (node->data.when_modifier.condition_lexeme) {
            free(node->data.when_modifier.condition_lexeme);
        }
    } else if (node->type == NODE_CHAN_INIT) {
        if (node->data.chan_init.channel_type) {
            free(node->data.chan_init.channel_type);
        }
    } else if (node->type == NODE_DEFER_BLOCK || node->type == NODE_ASYNC_DEFER_BLOCK) {
        if (node->data.defer_block.statements) {
            for (int i = 0; i < node->data.defer_block.statement_count; i++) {
                if (node->data.defer_block.statements[i]) {
                    free_concurrent_ast_node(node->data.defer_block.statements[i]);
                }
            }
            free(node->data.defer_block.statements);
        }
    }

    free(node);
}