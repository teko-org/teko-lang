#include "parser_visibility.h"
#include "parser_statements.h"
#include "parser_di.h"
#include "parser_ffi.h"
#include <stdio.h>
#include <stdlib.h>

static void vis_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Parses and wraps global declarations while enforcing visibility restrictions
VisibilityASTNode* parse_global_declaration_with_visibility(Parser* parser) {
    VisibilityKind current_vis = VIS_INTERNAL;

    // 1. Intercept and consume explicit modifiers if present
    if (parser->current_token.type == TOKEN_PUB) {
        current_vis = VIS_PROJECT_PUB;
        vis_advance(parser);
    } else if (parser->current_token.type == TOKEN_EXP) {
        current_vis = VIS_EXPORTED_EXP;
        vis_advance(parser);
    }

    // 2. Late allocation based on control variables for the wrapped node
    void* inner_node = NULL;
    int inner_type = 0;

    // Capture and route according to the subsequent structure
    if (parser->current_token.type == TOKEN_STRUCT) {
        // If it came from an extern block, FFI already manages it. If it is a pure structural type:
        inner_node = parse_extern_declaration(parser); // Reuse structural block mapping
        if (inner_node) inner_type = ((FFIASTNode*)inner_node)->type;
    }
    else if (parser->current_token.type == TOKEN_FN ||
            (parser->current_token.type == TOKEN_ASYNC && parser->peek_token.type == TOKEN_FN)) {
        inner_node = parse_statement(parser);
        if (inner_node) inner_type = ((StatementASTNode*)inner_node)->type;
    }
    else if (parser->current_token.type == TOKEN_INTERFACE) {
        // Pass false for the legacy export parameter since unified control now takes over
        inner_node = parse_di_interface(parser, (current_vis == VIS_EXPORTED_EXP));
        if (inner_node) inner_type = ((DIASTNode*)inner_node)->type;
    }
    else if (parser->current_token.type == TOKEN_SERVICE) {
        // Convention/Rule enforcement: service methods and blocks cannot be exported (exp)
        if (current_vis == VIS_EXPORTED_EXP) {
            fprintf(stderr, "[Syntax Error] Line %d: Services do not support the 'exp' modifier. Methods cannot be exported.\n",
                    parser->current_token.line);
            return NULL;
        }
        inner_node = parse_di_service(parser);
        if (inner_node) inner_type = ((DIASTNode*)inner_node)->type;
    }

    // If no valid structure was intercepted after the modifier, abort without a leak
    if (!inner_node) {
        return NULL;
    }

    // 3. Build the unified visibility structural node
    VisibilityASTNode* vis_node = (VisibilityASTNode*)malloc(sizeof(VisibilityASTNode));
    if (!vis_node) {
        // Safety fallback to prevent inner structure leaks if the heap allocation fails
        if (inner_type == NODE_FUNC_DECL || inner_type == NODE_VAR_DECL || inner_type == NODE_FOR_LOOP || inner_type == NODE_EXPR_STMT) {
            free_statement_ast_node((StatementASTNode*)inner_node);
        } else if (inner_type == NODE_DI_INTERFACE || inner_type == NODE_DI_SERVICE) {
            free_di_ast_node((DIASTNode*)inner_node);
        } else {
            free_ffi_ast_node((FFIASTNode*)inner_node);
        }
        return NULL;
    }

    vis_node->type = NODE_VISIBLE_DECL;
    vis_node->visibility = current_vis;
    vis_node->decorated_node = inner_node;
    vis_node->decorated_node_type = inner_type;

    return vis_node;
}

// Safe typed cascade deallocation based on the inner node discriminator
void free_visibility_ast_node(VisibilityASTNode* node) {
    if (!node) return;

    if (node->decorated_node) {
        int t = node->decorated_node_type;

        // Dispatch to the correct destructor based on the decorated node's enum value
        if (t == NODE_FUNC_DECL || t == NODE_VAR_DECL || t == NODE_FOR_LOOP || t == NODE_EXPR_STMT) {
            free_statement_ast_node((StatementASTNode*)node->decorated_node);
        }
        else if (t == NODE_DI_INTERFACE || t == NODE_DI_SERVICE || t == NODE_DI_DECORATOR) {
            free_di_ast_node((DIASTNode*)node->decorated_node);
        }
        else if (t == NODE_FFI_STRUCT || t == NODE_FFI_FUNCTION || t == NODE_FFI_BLOCK || t == NODE_FFI_INLINE_C) {
            free_ffi_ast_node((FFIASTNode*)node->decorated_node);
        }
    }

    free(node);
}