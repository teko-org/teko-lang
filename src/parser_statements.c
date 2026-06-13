#include "parser_statements.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void stmt_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Helper to consume multiple inner nodes within a block scope { ... }
static void parse_block_statements(Parser* parser, StatementASTNode*** list, int* count) {
    int cap = 4;
    *list = (StatementASTNode**)malloc(sizeof(StatementASTNode*) * cap);
    *count = 0;

    while (true) {
        if (parser->current_token.type == TOKEN_RBRACE || parser->current_token.type == TOKEN_EOF) {
            break;
        }

        if (*count >= cap) {
            cap *= 2;
            *list = (StatementASTNode**)realloc(*list, sizeof(StatementASTNode*) * cap);
        }

        StatementASTNode* stmt = parse_statement(parser);
        if (stmt != NULL) {
            (*list)[(*count)++] = stmt;
        } else {
            stmt_advance(parser); // Prevents stalls if an invalid token is encountered
        }
    }
}

// Parses local function declarations (including async fn main)
StatementASTNode* parse_function_declaration(Parser* parser, bool is_async) {
    stmt_advance(parser); // Consume 'fn'

    StatementASTNode* node = (StatementASTNode*)malloc(sizeof(StatementASTNode));
    node->type = NODE_FUNC_DECL;
    node->data.func_decl.fn_name = NULL;
    node->data.func_decl.is_async = is_async;
    node->data.func_decl.params = NULL;
    node->data.func_decl.param_count = 0;
    node->data.func_decl.return_type = NULL;
    node->data.func_decl.body_statements = NULL;
    node->data.func_decl.body_count = 0;

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        node->data.func_decl.fn_name = strdup(parser->current_token.lexeme);
        stmt_advance(parser);
    }

    // Scan function parameters: (ctx: arena, args: str[])
    if (parser->current_token.type == TOKEN_LPAREN) {
        stmt_advance(parser); // Consume '('
        int cap = 4;
        node->data.func_decl.params = (FuncParamNode*)malloc(sizeof(FuncParamNode) * cap);

        while (true) {
            if (parser->current_token.type == TOKEN_RPAREN || parser->current_token.type == TOKEN_EOF) {
                break;
            }

            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                if (node->data.func_decl.param_count >= cap) {
                    cap *= 2;
                    node->data.func_decl.params = (FuncParamNode*)realloc(node->data.func_decl.params, sizeof(FuncParamNode) * cap);
                }

                FuncParamNode* p = &node->data.func_decl.params[node->data.func_decl.param_count++];
                p->param_name = strdup(parser->current_token.lexeme);
                stmt_advance(parser); // Consume name

                if (parser->current_token.type == TOKEN_COLON) {
                    stmt_advance(parser); // Consume ':'
                    p->param_type = parse_complete_type_info(parser);
                } else {
                    p->param_type = NULL;
                }
            } else {
                stmt_advance(parser); // Consume invalid token inside parentheses
            }

            if (parser->current_token.type == TOKEN_COMMA) {
                stmt_advance(parser);
            }
        }
        if (parser->current_token.type == TOKEN_RPAREN) stmt_advance(parser);
    }

    if (parser->current_token.type == TOKEN_COLON) {
        stmt_advance(parser);
        node->data.func_decl.return_type = parse_complete_type_info(parser);
    }

    if (parser->current_token.type == TOKEN_LBRACE) {
        stmt_advance(parser);
        parse_block_statements(parser, &node->data.func_decl.body_statements, &node->data.func_decl.body_count);
        if (parser->current_token.type == TOKEN_RBRACE) stmt_advance(parser);
    }

    return node;
}

// Parses local variable statements: let/mut name [: type] = expr;
StatementASTNode* parse_variable_declaration(Parser* parser) {
    // 1. Simple local temporary variables for data collection
    bool is_mut = (parser->current_token.type == TOKEN_MUT);
    char* local_var_name = NULL;
    TypeInfo* local_var_type = NULL;
    char* local_initializer_raw = NULL;

    stmt_advance(parser); // Consume 'let' or 'mut'

    // Validate the mandatory identifier
    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        local_var_name = strdup(parser->current_token.lexeme);
        stmt_advance(parser);
    } else {
        // If it fails here, no structural node has been allocated on the heap yet.
        // There is no possibility of a leak.
        return NULL;
    }

    // Process optional type annotation ': type'
    if (parser->current_token.type == TOKEN_COLON) {
        stmt_advance(parser);
        local_var_type = parse_complete_type_info(parser);
        if (!local_var_type) {
            // If type parsing fails, free the cloned name and exit safely
            free(local_var_name);
            return NULL;
        }
    }

    // Process the initializer '='
    if (parser->current_token.type == TOKEN_ASSIGN) {
        stmt_advance(parser); // Consume '='

        int expr_start = parser->lexer->cursor;
        while (true) {
            if (parser->current_token.type == TOKEN_SEMICOLON ||
                parser->current_token.type == TOKEN_WHEN ||
                parser->current_token.type == TOKEN_EOF) {
                break;
            }
            stmt_advance(parser);
        }

        int expr_len = parser->lexer->cursor - expr_start;
        if (expr_len > 0) {
            local_initializer_raw = (char*)malloc(expr_len + 1);
            if (!local_initializer_raw) {
                free(local_var_name);
                if (local_var_type) free_type_info(local_var_type);
                return NULL;
            }

            strncpy(local_initializer_raw, &parser->lexer->source[expr_start], expr_len);
            local_initializer_raw[expr_len] = '\0';

            // Strip trailing formatting characters
            int last_idx = expr_len - 1;
            while (last_idx >= 0 && (local_initializer_raw[last_idx] == ';' ||
                                     local_initializer_raw[last_idx] == ' ' ||
                                     local_initializer_raw[last_idx] == '\r' ||
                                     local_initializer_raw[last_idx] == '\n')) {
                local_initializer_raw[last_idx] = '\0';
                last_idx--;
            }
        }
    }

    if (parser->current_token.type == TOKEN_SEMICOLON) {
        stmt_advance(parser);
    }

    // 2. LATE ALLOCATION: Did everything succeed? Only now do we instantiate the node on the heap!
    StatementASTNode* node = (StatementASTNode*)malloc(sizeof(StatementASTNode));
    if (!node) {
        free(local_var_name);
        if (local_var_type) free_type_info(local_var_type);
        if (local_initializer_raw) free(local_initializer_raw);
        return NULL;
    }

    node->type = NODE_VAR_DECL;
    node->data.var_decl.is_mutable = is_mut;
    node->data.var_decl.var_name = local_var_name;
    node->data.var_decl.var_type = local_var_type;
    node->data.var_decl.initializer_raw = local_initializer_raw;

    return node;
}

// Parses structured loops: for (mut i: i32; i < 10; i++) { ... }
StatementASTNode* parse_for_loop(Parser* parser) {
    // 1. Simple local temporary variables for data collection
    StatementASTNode* local_init_stmt = NULL;
    char* local_condition_raw = NULL;
    char* local_increment_raw = NULL;

    stmt_advance(parser); // Consume 'for'

    if (parser->current_token.type == TOKEN_LPAREN) {
        stmt_advance(parser); // Consume '('

        // Process the loop initializer
        if (parser->current_token.type == TOKEN_LET || parser->current_token.type == TOKEN_MUT) {
            local_init_stmt = parse_variable_declaration(parser);
            if (!local_init_stmt) {
                // Abort immediately without leaving orphan nodes
                return NULL;
            }
        }

        // Process the condition expression
        int cond_start = parser->lexer->cursor;
        while (true) {
            if (parser->current_token.type == TOKEN_SEMICOLON || parser->current_token.type == TOKEN_EOF) {
                break;
            }
            stmt_advance(parser);
        }

        int cond_len = (parser->lexer->cursor - cond_start) - 1;
        if (cond_len > 0) {
            local_condition_raw = (char*)malloc(cond_len + 1);
            if (!local_condition_raw) {
                if (local_init_stmt) free_statement_ast_node(local_init_stmt);
                return NULL;
            }
            strncpy(local_condition_raw, &parser->lexer->source[cond_start], cond_len);
            local_condition_raw[cond_len] = '\0';
        }

        if (parser->current_token.type == TOKEN_SEMICOLON) {
            stmt_advance(parser);
        }

        // Process the increment expression
        int inc_start = parser->lexer->cursor;
        while (true) {
            if (parser->current_token.type == TOKEN_RPAREN || parser->current_token.type == TOKEN_EOF) {
                break;
            }
            stmt_advance(parser);
        }

        int inc_len = (parser->lexer->cursor - inc_start) - 1;
        if (inc_len > 0) {
            local_increment_raw = (char*)malloc(inc_len + 1);
            if (!local_increment_raw) {
                if (local_init_stmt) free_statement_ast_node(local_init_stmt);
                if (local_condition_raw) free(local_condition_raw);
                return NULL;
            }
            strncpy(local_increment_raw, &parser->lexer->source[inc_start], inc_len);
            local_increment_raw[inc_len] = '\0';
        }

        if (parser->current_token.type == TOKEN_RPAREN) {
            stmt_advance(parser); // Consume ')'
        }
    } else {
        // If the opening parenthesis is missing, exit without any structural heap allocation
        return NULL;
    }

    // 2. LATE ALLOCATION: Instantiate the main structured body of the loop
    StatementASTNode* node = (StatementASTNode*)malloc(sizeof(StatementASTNode));
    if (!node) {
        if (local_init_stmt) free_statement_ast_node(local_init_stmt);
        if (local_condition_raw) free(local_condition_raw);
        if (local_increment_raw) free(local_increment_raw);
        return NULL;
    }

    node->type = NODE_FOR_LOOP;
    node->data.for_loop.init_stmt = local_init_stmt;
    node->data.for_loop.condition_raw = local_condition_raw;
    node->data.for_loop.increment_raw = local_increment_raw;
    node->data.for_loop.body_statements = NULL;
    node->data.for_loop.body_count = 0;

    // Capture the inner block '{ ... }' directly attached to the validated parent node
    if (parser->current_token.type == TOKEN_LBRACE) {
        stmt_advance(parser);
        parse_block_statements(parser, &node->data.for_loop.body_statements, &node->data.for_loop.body_count);
        if (parser->current_token.type == TOKEN_RBRACE) {
            stmt_advance(parser);
        }
    }

    return node;
}

// Central statement router (statement dispatcher)
StatementASTNode* parse_statement(Parser* parser) {
    if (parser->current_token.type == TOKEN_ASYNC && parser->peek_token.type == TOKEN_FN) {
        stmt_advance(parser); // Consume 'async'
        return parse_function_declaration(parser, true);
    }
    if (parser->current_token.type == TOKEN_FN) {
        return parse_function_declaration(parser, false);
    }
    if (parser->current_token.type == TOKEN_LET || parser->current_token.type == TOKEN_MUT) {
        return parse_variable_declaration(parser);
    }
    if (parser->current_token.type == TOKEN_FOR) {
        return parse_for_loop(parser);
    }

    // LEAK FIX 111 / GLOBAL SCOPE:
    // Only allocate and process the generic statement fallback if there is a valid token to consume.
    if (parser->current_token.type == TOKEN_EOF || parser->current_token.type == TOKEN_UNKNOWN) {
        return NULL;
    }

    StatementASTNode* node = (StatementASTNode*)malloc(sizeof(StatementASTNode));
    node->type = NODE_EXPR_STMT;

    int expr_start = parser->lexer->cursor;
    while (true) {
        if (parser->current_token.type == TOKEN_SEMICOLON ||
            parser->current_token.type == TOKEN_RBRACE ||
            parser->current_token.type == TOKEN_EOF) {
            break;
        }
        stmt_advance(parser);
    }
    int expr_len = parser->lexer->cursor - expr_start;
    node->data.expr_stmt.expression_raw = (char*)malloc(expr_len + 1);
    strncpy(node->data.expr_stmt.expression_raw, &parser->lexer->source[expr_start], expr_len);
    node->data.expr_stmt.expression_raw[expr_len] = '\0';

    if (parser->current_token.type == TOKEN_SEMICOLON) stmt_advance(parser);
    return node;
}

// Clean and null-safe recursive memory deallocation
void free_statement_ast_node(StatementASTNode* node) {
    if (!node) return;

    if (node->type == NODE_FUNC_DECL) {
        if (node->data.func_decl.fn_name) {
            free(node->data.func_decl.fn_name);
        }
        if (node->data.func_decl.return_type) {
            free_type_info(node->data.func_decl.return_type);
        }
        if (node->data.func_decl.params) {
            for (int i = 0; i < node->data.func_decl.param_count; i++) {
                if (node->data.func_decl.params[i].param_name) {
                    free(node->data.func_decl.params[i].param_name);
                }
                if (node->data.func_decl.params[i].param_type) {
                    free_type_info(node->data.func_decl.params[i].param_type);
                }
            }
            free(node->data.func_decl.params);
        }
        if (node->data.func_decl.body_statements) {
            for (int i = 0; i < node->data.func_decl.body_count; i++) {
                if (node->data.func_decl.body_statements[i]) {
                    free_statement_ast_node(node->data.func_decl.body_statements[i]);
                }
            }
            free(node->data.func_decl.body_statements);
        }
    }
    else if (node->type == NODE_VAR_DECL) {
        if (node->data.var_decl.var_name) {
            free(node->data.var_decl.var_name);
        }
        if (node->data.var_decl.var_type) {
            free_type_info(node->data.var_decl.var_type);
        }
        if (node->data.var_decl.initializer_raw) {
            free(node->data.var_decl.initializer_raw);
        }
    }
    else if (node->type == NODE_FOR_LOOP) {
        if (node->data.for_loop.init_stmt) {
            free_statement_ast_node(node->data.for_loop.init_stmt);
        }
        if (node->data.for_loop.condition_raw) {
            free(node->data.for_loop.condition_raw);
        }
        if (node->data.for_loop.increment_raw) {
            free(node->data.for_loop.increment_raw);
        }
        if (node->data.for_loop.body_statements) {
            for (int i = 0; i < node->data.for_loop.body_count; i++) {
                if (node->data.for_loop.body_statements[i]) {
                    free_statement_ast_node(node->data.for_loop.body_statements[i]);
                }
            }
            free(node->data.for_loop.body_statements);
        }
    }
    else if (node->type == NODE_EXPR_STMT) {
        if (node->data.expr_stmt.expression_raw) {
            free(node->data.expr_stmt.expression_raw);
        }
    }

    free(node);
}
