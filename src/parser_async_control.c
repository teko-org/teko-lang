#include "parser_async_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void async_ctrl_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Parses the internal cases of a standard/common switch
static void parse_statement_switch_cases(Parser* parser, AsyncControlASTNode* switch_node) {
    int cap = 4;
    switch_node->data.statement_switch.cases = (SwitchCaseNode*)malloc(sizeof(SwitchCaseNode) * cap);
    switch_node->data.statement_switch.case_count = 0;

    while (true) {
        if (parser->current_token.type == TOKEN_RBRACE || parser->current_token.type == TOKEN_EOF) {
            break;
        }

        if (switch_node->data.statement_switch.case_count >= cap) {
            cap *= 2;
            switch_node->data.statement_switch.cases = (SwitchCaseNode*)realloc(
                switch_node->data.statement_switch.cases, sizeof(SwitchCaseNode) * cap);
        }

        SwitchCaseNode* c_case = &switch_node->data.statement_switch.cases[switch_node->data.statement_switch.case_count];
        c_case->condition_pattern = strdup(parser->current_token.lexeme);
        c_case->case_body_raw = NULL;
        async_ctrl_advance(parser); // Consume the condition/pattern (e.g.: null, or _)

        // Consume the mandatory conditional arrow '=>'
        if (parser->current_token.type == TOKEN_ARROW) {
            async_ctrl_advance(parser);
        }

        // Capture the body associated with the traditional switch case (can end with ';' or a '{}' block)
        int body_start = parser->lexer->cursor;
        if (parser->current_token.type == TOKEN_LBRACE) {
            async_ctrl_advance(parser);
            int depth = 1;
            while (depth > 0 && parser->current_token.type != TOKEN_EOF) {
                if (parser->current_token.type == TOKEN_LBRACE) depth++;
                if (parser->current_token.type == TOKEN_RBRACE) depth--;
                async_ctrl_advance(parser);
            }
        } else {
            while (parser->current_token.type != TOKEN_SEMICOLON && parser->current_token.type != TOKEN_EOF) {
                async_ctrl_advance(parser);
            }
            if (parser->current_token.type == TOKEN_SEMICOLON) async_ctrl_advance(parser);
        }

        int body_len = parser->lexer->cursor - body_start;
        c_case->case_body_raw = (char*)malloc(body_len + 1);
        strncpy(c_case->case_body_raw, &parser->lexer->source[body_start], body_len);
        c_case->case_body_raw[body_len] = '\0';

        switch_node->data.statement_switch.case_count++;
    }
}

// Parses the internal arms of an inline switch (carried over from prior stage)
static void parse_switch_arms(Parser* parser, AsyncControlASTNode* switch_node) {
    int cap = 4;
    switch_node->data.inline_switch.arms = (SwitchArmNode*)malloc(sizeof(SwitchArmNode) * cap);
    switch_node->data.inline_switch.arm_count = 0;

    while (true) {
        if (parser->current_token.type == TOKEN_RBRACE || parser->current_token.type == TOKEN_EOF) {
            break;
        }

        if (switch_node->data.inline_switch.arm_count >= cap) {
            cap *= 2;
            switch_node->data.inline_switch.arms = (SwitchArmNode*)realloc(
                switch_node->data.inline_switch.arms, sizeof(SwitchArmNode) * cap);
        }

        SwitchArmNode* arm = &switch_node->data.inline_switch.arms[switch_node->data.inline_switch.arm_count];
        arm->pattern_lexeme = strdup(parser->current_token.lexeme);
        arm->condition_raw = NULL;
        arm->value_expr = NULL;
        async_ctrl_advance(parser);

        if (parser->current_token.type == TOKEN_WHEN) {
            async_ctrl_advance(parser);
            int cond_start = parser->lexer->cursor;
            while (parser->current_token.type != TOKEN_ARROW && parser->current_token.type != TOKEN_EOF) {
                async_ctrl_advance(parser);
            }
            int cond_len = parser->lexer->cursor - cond_start;
            arm->condition_raw = (char*)malloc(cond_len + 1);
            strncpy(arm->condition_raw, &parser->lexer->source[cond_start], cond_len);
            arm->condition_raw[cond_len] = '\0';
        }

        if (parser->current_token.type == TOKEN_ARROW) async_ctrl_advance(parser);

        int val_start = parser->lexer->cursor;
        while (parser->current_token.type != TOKEN_SEMICOLON && parser->current_token.type != TOKEN_EOF) {
            async_ctrl_advance(parser);
        }
        int val_len = parser->lexer->cursor - val_start;

        arm->value_expr = (AsyncControlASTNode*)malloc(sizeof(AsyncControlASTNode));
        arm->value_expr->type = NODE_WHEN_EXPR;
        arm->value_expr->data.when_expr.command_raw = (char*)malloc(val_len + 1);
        strncpy(arm->value_expr->data.when_expr.command_raw, &parser->lexer->source[val_start], val_len);
        arm->value_expr->data.when_expr.command_raw[val_len] = '\0';
        arm->value_expr->data.when_expr.condition_raw = NULL;

        if (parser->current_token.type == TOKEN_SEMICOLON) async_ctrl_advance(parser);
        switch_node->data.inline_switch.arm_count++;
    }
}

// Unified entry point to parse async control expressions and branches
AsyncControlASTNode* parse_async_control_statement(Parser* parser) {
    // NEW PREDICTIVE BLOCK: If the statement actually starts with 'switch', it is the scoped common switch
    if (parser->current_token.type == TOKEN_SWITCH) {
        AsyncControlASTNode* stmt_switch = (AsyncControlASTNode*)malloc(sizeof(AsyncControlASTNode));
        stmt_switch->type = NODE_STATEMENT_SWITCH;
        stmt_switch->data.statement_switch.control_expression_raw = NULL;
        stmt_switch->data.statement_switch.cases = NULL;
        stmt_switch->data.statement_switch.case_count = 0;

        async_ctrl_advance(parser); // Consume 'switch'

        // Isolate and capture the control expression inside parentheses (exs)
        if (parser->current_token.type == TOKEN_LPAREN) {
            int expr_start = parser->lexer->cursor;
            async_ctrl_advance(parser); // Consume '('
            while (parser->current_token.type != TOKEN_RPAREN && parser->current_token.type != TOKEN_EOF) {
                async_ctrl_advance(parser);
            }
            int expr_len = (parser->lexer->cursor - expr_start) - 1;
            stmt_switch->data.statement_switch.control_expression_raw = (char*)malloc(expr_len + 1);
            strncpy(stmt_switch->data.statement_switch.control_expression_raw, &parser->lexer->source[expr_start], expr_len);
            stmt_switch->data.statement_switch.control_expression_raw[expr_len] = '\0';

            if (parser->current_token.type == TOKEN_RPAREN) async_ctrl_advance(parser); // Consume ')'
        }

        // Process the structured body of standard switch cases
        if (parser->current_token.type == TOKEN_LBRACE) {
            async_ctrl_advance(parser); // Consume '{'
            parse_statement_switch_cases(parser, stmt_switch);
            if (parser->current_token.type == TOKEN_RBRACE) async_ctrl_advance(parser); // Consume '}'
        }

        return stmt_switch;
    }

    // --- IF NOT AN INITIAL SWITCH, FOLLOW THE ORIGINAL 'WHEN' EXPR AND 'INLINE SWITCH' FLOW ---
    AsyncControlASTNode* node = (AsyncControlASTNode*)malloc(sizeof(AsyncControlASTNode));
    node->type = NODE_WHEN_EXPR;
    node->data.when_expr.command_raw = NULL;
    node->data.when_expr.condition_raw = NULL;

    if (parser->peek_token.type == TOKEN_WHEN || parser->current_token.type == TOKEN_RETURN) {
        int cmd_start = parser->lexer->cursor;
        while (parser->current_token.type != TOKEN_WHEN &&
               parser->current_token.type != TOKEN_SEMICOLON &&
               parser->current_token.type != TOKEN_SWITCH &&
               parser->current_token.type != TOKEN_EOF) {
            async_ctrl_advance(parser);
        }

        int cmd_len = parser->lexer->cursor - cmd_start;
        node->data.when_expr.command_raw = (char*)malloc(cmd_len + 1);
        strncpy(node->data.when_expr.command_raw, &parser->lexer->source[cmd_start], cmd_len);
        node->data.when_expr.command_raw[cmd_len] = '\0';

        if (parser->current_token.type == TOKEN_WHEN) {
            node->type = NODE_WHEN_EXPR;
            async_ctrl_advance(parser);

            int cond_start = parser->lexer->cursor;
            while (parser->current_token.type != TOKEN_SEMICOLON &&
                   parser->current_token.type != TOKEN_LBRACE &&
                   parser->current_token.type != TOKEN_EOF) {
                async_ctrl_advance(parser);
            }
            int cond_len = parser->lexer->cursor - cond_start;
            node->data.when_expr.condition_raw = (char*)malloc(cond_len + 1);
            strncpy(node->data.when_expr.condition_raw, &parser->lexer->source[cond_start], cond_len);
            node->data.when_expr.condition_raw[cond_len] = '\0';
        }

        if (parser->current_token.type == TOKEN_SWITCH) {
            AsyncControlASTNode* switch_node = (AsyncControlASTNode*)malloc(sizeof(AsyncControlASTNode));
            switch_node->type = NODE_INLINE_SWITCH;
            switch_node->data.inline_switch.target_expression_raw = strdup(node->data.when_expr.command_raw);
            switch_node->data.inline_switch.arms = NULL;
            switch_node->data.inline_switch.arm_count = 0;

            async_ctrl_advance(parser);

                if (parser->current_token.type == TOKEN_LBRACE) {
                async_ctrl_advance(parser);
                parse_switch_arms(parser, switch_node);
                if (parser->current_token.type == TOKEN_RBRACE) async_ctrl_advance(parser);
            }

            free_async_control_ast_node(node);
            node = switch_node;
        }
    }

    if (parser->current_token.type == TOKEN_RAISED) {
        AsyncControlASTNode* raised_node = (AsyncControlASTNode*)malloc(sizeof(AsyncControlASTNode));
        raised_node->type = NODE_RAISED_CATCH;
        raised_node->data.raised_catch.protected_block = node;
        raised_node->data.raised_catch.error_variable_name = NULL;
        raised_node->data.raised_catch.catch_body_raw = NULL;

        async_ctrl_advance(parser); // Consume 'raised'

        if (parser->current_token.type == TOKEN_LPAREN) {
            async_ctrl_advance(parser); // Consume '('
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                raised_node->data.raised_catch.error_variable_name = strdup(parser->current_token.lexeme);
                async_ctrl_advance(parser);
            }
            if (parser->current_token.type == TOKEN_RPAREN) async_ctrl_advance(parser); // Consume ')'
        }

        if (parser->current_token.type == TOKEN_LBRACE) {
            async_ctrl_advance(parser); // Consume '{'
            int body_start = parser->lexer->cursor;
            int depth = 1;

            while (true) {
                if (depth <= 0 || parser->current_token.type == TOKEN_EOF) break;
                if (parser->current_token.type == TOKEN_LBRACE) depth++;
                if (parser->current_token.type == TOKEN_RBRACE) depth--;
                async_ctrl_advance(parser);
            }

            int body_len = (parser->lexer->cursor - body_start) - 1;
            raised_node->data.raised_catch.catch_body_raw = (char*)malloc(body_len + 1);
            strncpy(raised_node->data.raised_catch.catch_body_raw, &parser->lexer->source[body_start], body_len);
            raised_node->data.raised_catch.catch_body_raw[body_len] = '\0';
        }

        if (parser->current_token.type == TOKEN_SEMICOLON) async_ctrl_advance(parser);
        node = raised_node;
    }

    return node;
}

// Updated memory deallocation with safe disposal of the common switch and null pointer checks
void free_async_control_ast_node(AsyncControlASTNode* node) {
    if (!node) return;

    if (node->type == NODE_WHEN_EXPR) {
        if (node->data.when_expr.command_raw) free(node->data.when_expr.command_raw);
        if (node->data.when_expr.condition_raw) free(node->data.when_expr.condition_raw);
    }
    else if (node->type == NODE_INLINE_SWITCH) {
        if (node->data.inline_switch.target_expression_raw) free(node->data.inline_switch.target_expression_raw);
        if (node->data.inline_switch.arms) {
            for (int i = 0; i < node->data.inline_switch.arm_count; i++) {
                if (node->data.inline_switch.arms[i].pattern_lexeme) free(node->data.inline_switch.arms[i].pattern_lexeme);
                if (node->data.inline_switch.arms[i].condition_raw) free(node->data.inline_switch.arms[i].condition_raw);
                if (node->data.inline_switch.arms[i].value_expr) free_async_control_ast_node(node->data.inline_switch.arms[i].value_expr);
            }
            free(node->data.inline_switch.arms);
        }
    }
    else if (node->type == NODE_STATEMENT_SWITCH) {
        if (node->data.statement_switch.control_expression_raw) free(node->data.statement_switch.control_expression_raw);
        if (node->data.statement_switch.cases) {
            for (int i = 0; i < node->data.statement_switch.case_count; i++) {
                if (node->data.statement_switch.cases[i].condition_pattern) free(node->data.statement_switch.cases[i].condition_pattern);
                if (node->data.statement_switch.cases[i].case_body_raw) free(node->data.statement_switch.cases[i].case_body_raw);
            }
            free(node->data.statement_switch.cases);
        }
    }
    else if (node->type == NODE_RAISED_CATCH) {
        if (node->data.raised_catch.protected_block) free_async_control_ast_node(node->data.raised_catch.protected_block);
        if (node->data.raised_catch.error_variable_name) free(node->data.raised_catch.error_variable_name);
        if (node->data.raised_catch.catch_body_raw) free(node->data.raised_catch.catch_body_raw);
    }

    free(node);
}