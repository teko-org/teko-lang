#include "parser_extensions.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void ext_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Parses the inner members of the extend block (methods and inline operators)
static void parse_extension_members(Parser* parser, ExtensionASTNode* ext_node) {
    int cap = 4;
    ext_node->members = (ExtensionMemberNode*)malloc(sizeof(ExtensionMemberNode) * cap);
    ext_node->member_count = 0;

    while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
        if (ext_node->member_count >= cap) {
            cap *= 2;
            ext_node->members = (ExtensionMemberNode*)realloc(ext_node->members, sizeof(ExtensionMemberNode) * cap);
        }

        ExtensionMemberNode* member = &ext_node->members[ext_node->member_count];
        member->name = NULL;
        member->return_type = NULL;
        member->param_name = NULL;
        member->param_type = NULL;
        member->is_inline = false;
        member->body_raw = NULL;

        // Case 1: Operator overload -> operator +(string: str) : str => ...
        if (parser->current_token.type == TOKEN_OPERATOR) {
            member->type = NODE_EXTENSION_OPERATOR;
            ext_advance(parser); // Consume 'operator'

            // Capture the operator lexeme (e.g.: "+", "-", etc.)
            member->name = strdup(parser->current_token.lexeme);
            ext_advance(parser);

            // Process the operator's single parameter
            if (parser->current_token.type == TOKEN_LPAREN) {
                ext_advance(parser); // Consume '('
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    member->param_name = strdup(parser->current_token.lexeme);
                    ext_advance(parser);
                }
                if (parser->current_token.type == TOKEN_COLON) {
                    ext_advance(parser); // Consume ':'
                    member->param_type = parse_complete_type_info(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) ext_advance(parser); // Consume ')'
            }
        }
        // Case 2: Normal extension method -> append_line(string: str) : str { ... }
        else if (parser->current_token.type == TOKEN_IDENTIFIER) {
            member->type = NODE_EXTENSION_METHOD;
            member->name = strdup(parser->current_token.lexeme);
            ext_advance(parser); // Consume the method name

            // Skip method parameters generically to focus on the structure
            if (parser->current_token.type == TOKEN_LPAREN) {
                ext_advance(parser);
                while (parser->current_token.type != TOKEN_RPAREN && parser->current_token.type != TOKEN_EOF) {
                    ext_advance(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) ext_advance(parser);
            }
        } else {
            ext_advance(parser);
            continue;
        }

        // Capture the return type common to both: ': str'
        if (parser->current_token.type == TOKEN_COLON) {
            ext_advance(parser); // Consume ':'
            member->return_type = parse_complete_type_info(parser);
        }

        // Determine whether the body is inline (=>) or a block ({})
        if (parser->current_token.type == TOKEN_ARROW) {
            member->is_inline = true;
            ext_advance(parser); // Consume '=>'

            // Capture everything up to the semicolon as a literal expression
            int expr_start = parser->lexer->cursor;
            while (parser->current_token.type != TOKEN_SEMICOLON && parser->current_token.type != TOKEN_EOF) {
                ext_advance(parser);
            }
            int expr_len = parser->lexer->cursor - expr_start;
            member->body_raw = (char*)malloc(expr_len + 1);
            strncpy(member->body_raw, &parser->lexer->source[expr_start], expr_len);
            member->body_raw[expr_len] = '\0';

            if (parser->current_token.type == TOKEN_SEMICOLON) ext_advance(parser);
        }
        else if (parser->current_token.type == TOKEN_LBRACE) {
            member->is_inline = false;
            ext_advance(parser); // Consume '{'

            // Capture the block respecting brace nesting
            int block_start = parser->lexer->cursor;
            int depth = 1;
            while (depth > 0 && parser->current_token.type != TOKEN_EOF) {
                if (parser->current_token.type == TOKEN_LBRACE) depth++;
                if (parser->current_token.type == TOKEN_RBRACE) depth--;
                ext_advance(parser);
            }
            int block_len = (parser->lexer->cursor - block_start) - 1; // ignore the closing brace
            member->body_raw = (char*)malloc(block_len + 1);
            strncpy(member->body_raw, &parser->lexer->source[block_start], block_len);
            member->body_raw[block_len] = '\0';
        }

        ext_node->member_count++;
    }
}

// Entry point: extend(self: string) { ... }
ExtensionASTNode* parse_type_extension(Parser* parser) {
    ext_advance(parser); // Consume 'extend'

    ExtensionASTNode* node = (ExtensionASTNode*)malloc(sizeof(ExtensionASTNode));
    node->type = NODE_TYPE_EXTENSION;
    node->self_param_name = NULL;
    node->self_type = NULL;
    node->members = NULL;
    node->member_count = 0;

    // Process the receiver binding signature: (self: string)
    if (parser->current_token.type == TOKEN_LPAREN) {
        ext_advance(parser); // Consume '('
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            node->self_param_name = strdup(parser->current_token.lexeme);
            ext_advance(parser); // Consume 'self'
        }
        if (parser->current_token.type == TOKEN_COLON) {
            ext_advance(parser); // Consume ':'
            node->self_type = parse_complete_type_info(parser); // Identifies "string"
        }
        if (parser->current_token.type == TOKEN_RPAREN) ext_advance(parser); // Consume ')'
    }

    // Process the body containing the extension definitions
    if (parser->current_token.type == TOKEN_LBRACE) {
        ext_advance(parser); // Consume '{'
        parse_extension_members(parser, node);
        if (parser->current_token.type == TOKEN_RBRACE) ext_advance(parser); // Consume '}'
    }

    return node;
}

// Safe recursive memory deallocation
void free_extension_ast_node(ExtensionASTNode* node) {
    if (!node) return;
    if (node->self_param_name) free(node->self_param_name);
    if (node->self_type) free_type_info(node->self_type);

    if (node->members) {
        for (int i = 0; i < node->member_count; i++) {
            if (node->members[i].name) free(node->members[i].name);
            if (node->members[i].return_type) free_type_info(node->members[i].return_type);
            if (node->members[i].param_name) free(node->members[i].param_name);
            if (node->members[i].param_type) free_type_info(node->members[i].param_type);
            if (node->members[i].body_raw) free(node->members[i].body_raw);
        }
        free(node->members);
    }
    free(node);
}