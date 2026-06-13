#include "parser_messaging.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void msg_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Parses the internal properties of commands, queries, and notifications
static void parse_message_properties(Parser* parser, MessageProperty** props, int* count) {
    int cap = 4;
    *props = (MessageProperty*)malloc(sizeof(MessageProperty) * cap);
    *count = 0;

    if (parser->current_token.type == TOKEN_LBRACE) {
        msg_advance(parser); // Consume '{'

        while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
            bool is_req = false;
            bool is_mut = false;

            if (parser->current_token.type == TOKEN_REQD) {
                is_req = true;
                msg_advance(parser);
            }
            if (parser->current_token.type == TOKEN_MUT) {
                is_mut = true;
                msg_advance(parser);
            }

            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                if (*count >= cap) {
                    cap *= 2;
                    *props = (MessageProperty*)realloc(*props, sizeof(MessageProperty) * cap);
                }

                (*props)[*count].prop_name = strdup(parser->current_token.lexeme);
                (*props)[*count].is_required = is_req;
                (*props)[*count].is_mutable = is_mut;
                msg_advance(parser); // Consume property name

                if (parser->current_token.type == TOKEN_COLON) {
                    msg_advance(parser); // Consume ':'
                    (*props)[*count].prop_type = parse_complete_type_info(parser);
                } else {
                    (*props)[*count].prop_type = NULL;
                }
                (*count)++;
            }

            if (parser->current_token.type == TOKEN_SEMICOLON) {
                msg_advance(parser);
            }
        }
        if (parser->current_token.type == TOKEN_RBRACE) {
            msg_advance(parser); // Consume '}'
        }
    }
}

// Entry point to parse structures: command, query, and notification
MessagingASTNode* parse_messaging_structure(Parser* parser) {
    auto node = (MessagingASTNode*)malloc(sizeof(MessagingASTNode));
    if (!node) return NULL;
    node->name = NULL;

    if (strcmp(parser->current_token.lexeme, "command") == 0) {
        node->type = NODE_MSG_COMMAND;
    } else if (strcmp(parser->current_token.lexeme, "query") == 0) {
        node->type = NODE_MSG_QUERY;
    } else if (strcmp(parser->current_token.lexeme, "notification") == 0) {
        node->type = NODE_MSG_NOTIFICATION;
    } else {
        node->type = TOKEN_UNKNOWN;
    }
    msg_advance(parser);

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        node->name = strdup(parser->current_token.lexeme);
        msg_advance(parser);
    }

    if (node->type == NODE_MSG_QUERY && parser->current_token.type == TOKEN_COLON) {
        msg_advance(parser);
        node->data.query_struct.return_intent_type = parse_complete_type_info(parser);
        parse_message_properties(parser, &node->data.query_struct.properties, &node->data.query_struct.property_count);
    } else {
        if (node->type == NODE_MSG_QUERY) node->data.query_struct.return_intent_type = NULL;
        parse_message_properties(parser, &node->data.msg_struct.properties, &node->data.msg_struct.property_count);
    }

    return node;
}

// Entry point: handler for Name { ... } with dependency mapping and their arenas
MessagingASTNode* parse_messaging_handler(Parser* parser) {
    msg_advance(parser); // Consume 'handler'

    if (strcmp(parser->current_token.lexeme, "for") == 0) {
        msg_advance(parser);
    }

    auto node = (MessagingASTNode*)malloc(sizeof(MessagingASTNode));
    if (!node) return NULL;

    node->type = NODE_MSG_HANDLER;
    node->name = NULL;
    node->data.msg_handler.is_async_handler = false;
    node->data.msg_handler.handle_param_name = NULL;
    node->data.msg_handler.handle_return_type = NULL;
    node->data.msg_handler.dependencies = NULL;
    node->data.msg_handler.dependency_count = 0;

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        node->name = strdup(parser->current_token.lexeme);
        msg_advance(parser);
    }

    if (parser->current_token.type == TOKEN_LBRACE) {
        msg_advance(parser);

        if (parser->current_token.type == TOKEN_ASYNC) {
            node->data.msg_handler.is_async_handler = true;
            msg_advance(parser);
        }

        if (strcmp(parser->current_token.lexeme, "handle") == 0) {
            msg_advance(parser);

            if (parser->current_token.type == TOKEN_LPAREN) {
                msg_advance(parser);
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    node->data.msg_handler.handle_param_name = strdup(parser->current_token.lexeme);
                    msg_advance(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) msg_advance(parser);
            }
        }

        // Process native 'with (DILifetime Type name)' injection directing the arena lifetime
        if (strcmp(parser->current_token.lexeme, "with") == 0) {
            msg_advance(parser); // Consume 'with'
            if (parser->current_token.type == TOKEN_LPAREN) {
                msg_advance(parser); // Consume '('
                int cap = 2;
                node->data.msg_handler.dependencies = (HandlerDependency*)malloc(sizeof(HandlerDependency) * cap);

                while (parser->current_token.type != TOKEN_RPAREN && parser->current_token.type != TOKEN_EOF) {
                    if (node->data.msg_handler.dependency_count >= cap) {
                        cap *= 2;
                        node->data.msg_handler.dependencies = (HandlerDependency*)realloc(
                            node->data.msg_handler.dependencies, sizeof(HandlerDependency) * cap);
                    }

                    HandlerDependency* d = &node->data.msg_handler.dependencies[node->data.msg_handler.dependency_count];
                    d->dep_type = NULL;
                    d->dep_name = NULL;
                    d->lifetime = LIFETIME_TRANSIENT; // Default bound to the destination arena

                    // Optionally read explicit modifiers: singleton, scoped, transient [1]
                    if (parser->current_token.type == TOKEN_IDENTIFIER) {
                        if (strcmp(parser->current_token.lexeme, "singleton") == 0) {
                            d->lifetime = LIFETIME_SINGLETON;
                            msg_advance(parser);
                        } else if (strcmp(parser->current_token.lexeme, "scoped") == 0) {
                            d->lifetime = LIFETIME_SCOPED;
                            msg_advance(parser);
                        } else if (strcmp(parser->current_token.lexeme, "transient") == 0) {
                            d->lifetime = LIFETIME_TRANSIENT;
                            msg_advance(parser);
                        }
                    }

                    if (parser->current_token.type == TOKEN_IDENTIFIER) {
                        d->dep_type = strdup(parser->current_token.lexeme); // Interface (e.g.: IEmailSender) [1]
                        msg_advance(parser);

                        if (parser->current_token.type == TOKEN_IDENTIFIER) {
                            d->dep_name = strdup(parser->current_token.lexeme); // Name (e.g.: sender) [1]
                            msg_advance(parser);
                        }
                        node->data.msg_handler.dependency_count++;
                    } else {
                        msg_advance(parser);
                    }
                    if (parser->current_token.type == TOKEN_COMMA) msg_advance(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) msg_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_COLON) {
            msg_advance(parser);
            node->data.msg_handler.handle_return_type = parse_complete_type_info(parser);
        }

        while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
            msg_advance(parser);
        }
        if (parser->current_token.type == TOKEN_RBRACE) msg_advance(parser);
    }

    return node;
}

// Clean cascade memory deallocation against leaks
void free_messaging_ast_node(MessagingASTNode* node) {
    if (!node) return;
    if (node->name) free(node->name);

    if (node->type == NODE_MSG_COMMAND || node->type == NODE_MSG_NOTIFICATION) {
        if (node->data.msg_struct.properties) {
            for (int i = 0; i < node->data.msg_struct.property_count; i++) {
                if (node->data.msg_struct.properties[i].prop_name) free(node->data.msg_struct.properties[i].prop_name);
                if (node->data.msg_struct.properties[i].prop_type) free_type_info(node->data.msg_struct.properties[i].prop_type);
            }
            free(node->data.msg_struct.properties);
        }
    } else if (node->type == NODE_MSG_QUERY) {
        if (node->data.query_struct.return_intent_type) free_type_info(node->data.query_struct.return_intent_type);
        if (node->data.query_struct.properties) {
            for (int i = 0; i < node->data.query_struct.property_count; i++) {
                if (node->data.query_struct.properties[i].prop_name) free(node->data.query_struct.properties[i].prop_name);
                if (node->data.query_struct.properties[i].prop_type) free_type_info(node->data.query_struct.properties[i].prop_type);
            }
            free(node->data.query_struct.properties);
        }
    } else if (node->type == NODE_MSG_HANDLER) {
        if (node->data.msg_handler.handle_param_name) free(node->data.msg_handler.handle_param_name);
        if (node->data.msg_handler.handle_return_type) free_type_info(node->data.msg_handler.handle_return_type);
        if (node->data.msg_handler.dependencies) {
            for (int i = 0; i < node->data.msg_handler.dependency_count; i++) {
                if (node->data.msg_handler.dependencies[i].dep_type) free(node->data.msg_handler.dependencies[i].dep_type);
                if (node->data.msg_handler.dependencies[i].dep_name) free(node->data.msg_handler.dependencies[i].dep_name);
            }
            free(node->data.msg_handler.dependencies);
        }
    }
    free(node);
}