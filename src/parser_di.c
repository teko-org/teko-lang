#include "parser_di.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void di_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Parses the list of method signatures inside DI blocks
static void parse_di_methods(Parser* parser, DIMethodSignature** methods, int* count) {
    int cap = 4;
    *methods = (DIMethodSignature*)malloc(sizeof(DIMethodSignature) * cap);
    *count = 0;

    while (true) {
        if (parser->current_token.type == TOKEN_RBRACE || parser->current_token.type == TOKEN_EOF) {
            break;
        }

        bool is_pub = false;
        if (parser->current_token.type == TOKEN_PUB) {
            is_pub = true;
            di_advance(parser);
        }

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            if (*count >= cap) {
                cap *= 2;
                *methods = (DIMethodSignature*)realloc(*methods, sizeof(DIMethodSignature) * cap);
            }

            (*methods)[*count].method_name = strdup(parser->current_token.lexeme);
            (*methods)[*count].is_public = is_pub;
            di_advance(parser);

            if (parser->current_token.type == TOKEN_LPAREN) {
                di_advance(parser);
                while (parser->current_token.type != TOKEN_RPAREN && parser->current_token.type != TOKEN_EOF) {
                    di_advance(parser);
                }
                if (parser->current_token.type == TOKEN_RPAREN) di_advance(parser);
            }

            if (parser->current_token.type == TOKEN_COLON) {
                di_advance(parser);
                (*methods)[*count].return_type = parse_complete_type_info(parser);
            } else {
                (*methods)[*count].return_type = NULL;
            }

            if (parser->current_token.type == TOKEN_SEMICOLON) {
                di_advance(parser);
            } else if (parser->current_token.type == TOKEN_ARROW) {
                di_advance(parser);
                while (parser->current_token.type != TOKEN_SEMICOLON && parser->current_token.type != TOKEN_EOF) {
                    di_advance(parser);
                }
                if (parser->current_token.type == TOKEN_SEMICOLON) di_advance(parser);
            } else if (parser->current_token.type == TOKEN_LBRACE) {
                di_advance(parser);
                int brace_depth = 1;
                while (brace_depth > 0 && parser->current_token.type != TOKEN_EOF) {
                    if (parser->current_token.type == TOKEN_LBRACE) brace_depth++;
                    if (parser->current_token.type == TOKEN_RBRACE) brace_depth--;
                    di_advance(parser);
                }
            }
            (*count)++;
        } else {
            di_advance(parser);
        }
    }
}

// Parses: [exp] interface IEmailSender { ... }
DIASTNode* parse_di_interface(Parser* parser, bool is_exported) {
    di_advance(parser);

    auto node = (DIASTNode*)malloc(sizeof(DIASTNode));
    if (!node) return NULL;
    node->type = NODE_DI_INTERFACE;
    node->name = NULL;
    node->data.di_interface.is_exported = is_exported;
    node->data.di_interface.methods = NULL;
    node->data.di_interface.method_count = 0;

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        node->name = strdup(parser->current_token.lexeme);
        di_advance(parser);
    }

    if (parser->current_token.type == TOKEN_LBRACE) {
        di_advance(parser);
        parse_di_methods(parser, &node->data.di_interface.methods, &node->data.di_interface.method_count);
        if (parser->current_token.type == TOKEN_RBRACE) di_advance(parser);
    }

    return node;
}

// Parses: service EmailSender : IEmailSender { ... }
DIASTNode* parse_di_service(Parser* parser) {
    di_advance(parser);

    auto node = (DIASTNode*)malloc(sizeof(DIASTNode));
    if (!node) return NULL;
    node->type = NODE_DI_SERVICE;
    node->name = NULL;
    node->data.di_service.implements_interface = NULL;
    node->data.di_service.methods = NULL;
    node->data.di_service.method_count = 0;

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        node->name = strdup(parser->current_token.lexeme);
        di_advance(parser);
    }

    if (parser->current_token.type == TOKEN_COLON) {
        di_advance(parser);
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            node->data.di_service.implements_interface = strdup(parser->current_token.lexeme);
            di_advance(parser);
        }
    }

    if (parser->current_token.type == TOKEN_LBRACE) {
        di_advance(parser);
        parse_di_methods(parser, &node->data.di_service.methods, &node->data.di_service.method_count);
        if (parser->current_token.type == TOKEN_RBRACE) di_advance(parser);
    }

    return node;
}

// Parses: decorates(next: IEmailSender, 0) { ... }
DIASTNode* parse_di_decorator(Parser* parser) {
    di_advance(parser);

    auto node = (DIASTNode*)malloc(sizeof(DIASTNode));
    if (!node) return NULL;
    node->type = NODE_DI_DECORATOR;
    node->name = strdup("decorator");
    node->data.di_decorator.next_param_name = NULL;
    node->data.di_decorator.target_interface = NULL;
    node->data.di_decorator.precedence_order = 0;
    node->data.di_decorator.methods = NULL;
    node->data.di_decorator.method_count = 0;

    if (parser->current_token.type == TOKEN_LPAREN) {
        di_advance(parser);

        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            node->data.di_decorator.next_param_name = strdup(parser->current_token.lexeme);
            di_advance(parser);
        }

        if (parser->current_token.type == TOKEN_COLON) {
            di_advance(parser);
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                node->data.di_decorator.target_interface = strdup(parser->current_token.lexeme);
                di_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_COMMA) {
            di_advance(parser);
            if (parser->current_token.type == TOKEN_LIT_INT) {
                node->data.di_decorator.precedence_order = atoi(parser->current_token.lexeme);
                di_advance(parser);
            }
        }

        if (parser->current_token.type == TOKEN_RPAREN) di_advance(parser);
    }

    if (parser->current_token.type == TOKEN_LBRACE) {
        di_advance(parser);
        parse_di_methods(parser, &node->data.di_decorator.methods, &node->data.di_decorator.method_count);
        if (parser->current_token.type == TOKEN_RBRACE) di_advance(parser);
    }

    return node;
}

// Parses the 'with' clause with explicit support for arena allocation scopes
static void parse_handler_dependencies_with_arenas(Parser* parser, HandlerDependency** deps, int* count) {
    int cap = 2;
    *deps = (HandlerDependency*)malloc(sizeof(HandlerDependency) * cap);
    *count = 0;

    if (parser->current_token.type == TOKEN_LPAREN) {
        di_advance(parser); // Consume '('

        while (true) {
            if (parser->current_token.type == TOKEN_RPAREN || parser->current_token.type == TOKEN_EOF) {
                break;
            }

            if (*count >= cap) {
                cap *= 2;
                *deps = (HandlerDependency*)realloc(*deps, sizeof(HandlerDependency) * cap);
            }

            HandlerDependency* d = &(*deps)[*count];
            d->dep_type = NULL;
            d->dep_name = NULL;
            d->lifetime = LIFETIME_TRANSIENT; // Allocation in the destination arena by default

            // Intercept and consume explicit arena scope qualifiers if typed by the user
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                if (strcmp(parser->current_token.lexeme, "singleton") == 0) {
                    d->lifetime = LIFETIME_SINGLETON;
                    di_advance(parser);
                } else if (strcmp(parser->current_token.lexeme, "scoped") == 0) {
                    d->lifetime = LIFETIME_SCOPED;
                    di_advance(parser);
                } else if (strcmp(parser->current_token.lexeme, "transient") == 0) {
                    d->lifetime = LIFETIME_TRANSIENT;
                    di_advance(parser);
                }
            }

            // Capture the structural type of the interface to be injected (e.g.: IEmailSender)
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                d->dep_type = strdup(parser->current_token.lexeme);
                di_advance(parser);

                // Capture the injection variable name (e.g.: sender)
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    d->dep_name = strdup(parser->current_token.lexeme);
                    di_advance(parser);
                }
                (*count)++;
            } else {
                di_advance(parser); // Cleanup in case garbage tokens appear
            }

            if (parser->current_token.type == TOKEN_COMMA) di_advance(parser);
        }
        if (parser->current_token.type == TOKEN_RPAREN) di_advance(parser);
    }
}

// Unified DI subsystem memory deallocation against leaks
void free_di_ast_node(DIASTNode* node) {
    if (!node) return;
    if (node->name) free(node->name);

    if (node->type == NODE_DI_INTERFACE) {
        if (node->data.di_interface.methods) {
            for (int i = 0; i < node->data.di_interface.method_count; i++) {
                if (node->data.di_interface.methods[i].method_name) {
                    free(node->data.di_interface.methods[i].method_name);
                }
                if (node->data.di_interface.methods[i].return_type) {
                    free_type_info(node->data.di_interface.methods[i].return_type);
                }
            }
            free(node->data.di_interface.methods);
        }
    } else if (node->type == NODE_DI_SERVICE) {
        if (node->data.di_service.implements_interface) {
            free(node->data.di_service.implements_interface);
        }
        if (node->data.di_service.methods) {
            for (int i = 0; i < node->data.di_service.method_count; i++) {
                if (node->data.di_service.methods[i].method_name) {
                    free(node->data.di_service.methods[i].method_name);
                }
                if (node->data.di_service.methods[i].return_type) {
                    free_type_info(node->data.di_service.methods[i].return_type);
                }
            }
            free(node->data.di_service.methods);
        }
    } else if (node->type == NODE_DI_DECORATOR) {
        if (node->data.di_decorator.next_param_name) {
            free(node->data.di_decorator.next_param_name);
        }
        if (node->data.di_decorator.target_interface) {
            free(node->data.di_decorator.target_interface);
        }
        if (node->data.di_decorator.methods) {
            for (int i = 0; i < node->data.di_decorator.method_count; i++) {
                if (node->data.di_decorator.methods[i].method_name) {
                    free(node->data.di_decorator.methods[i].method_name);
                }
                if (node->data.di_decorator.methods[i].return_type) {
                    free_type_info(node->data.di_decorator.methods[i].return_type);
                }
            }
            free(node->data.di_decorator.methods);
        }
    } else {
        // Generic fallback handling for nodes associated with messages or handlers
        if (node->data.msg_handler.handle_param_name) {
            free(node->data.msg_handler.handle_param_name);
        }
        if (node->data.msg_handler.handle_return_type) {
            free_type_info(node->data.msg_handler.handle_return_type);
        }
        if (node->data.msg_handler.dependencies) {
            for (int i = 0; i < node->data.msg_handler.dependency_count; i++) {
                if (node->data.msg_handler.dependencies[i].dep_type) {
                    free(node->data.msg_handler.dependencies[i].dep_type);
                }
                if (node->data.msg_handler.dependencies[i].dep_name) {
                    free(node->data.msg_handler.dependencies[i].dep_name);
                }
            }
            free(node->data.msg_handler.dependencies);
        }
    }

    free(node);
}
