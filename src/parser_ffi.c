#include "parser_ffi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Advances tokens using the original parser state
static void ffi_advance(Parser* parser) {
    parser->current_token = parser->peek_token;
    parser->peek_token = lexer_next_token(parser->lexer);
}

// Helper to capture complex types (e.g.: ptr<void>, ptr<ptr<char>>, str[])
static char* parse_complex_type(Parser* parser) {
    char type_buffer[512] = {0};

    if (parser->current_token.type != TOKEN_IDENTIFIER) {
        return strdup("void");
    }

    strcat(type_buffer, parser->current_token.lexeme);
    ffi_advance(parser);

    // Handle recursive generic/pointer types such as ptr<ptr<char>> or ptr<ExternalStructure>
    if (parser->current_token.type == TOKEN_LT) {
        strcat(type_buffer, "<");
        ffi_advance(parser); // Consume '<'

        char* inner_type = parse_complex_type(parser);
        strcat(type_buffer, inner_type);
        free(inner_type);

        if (parser->current_token.type == TOKEN_GT) {
            strcat(type_buffer, ">");
            ffi_advance(parser); // Consume '>'
        }
    }

    // Handle arrays like str[]
    if (parser->current_token.type == TOKEN_LBRACKET) {
        ffi_advance(parser); // Consume '['
        if (parser->current_token.type == TOKEN_RBRACKET) {
            strcat(type_buffer, "[]");
            ffi_advance(parser); // Consume ']'
        }
    }

    return strdup(type_buffer);
}

// Parses a single FFI function signature
static void parse_single_function_signature(Parser* parser, FFIFunctionNode* fn) {
    ffi_advance(parser); // Consume 'fn'

    if (parser->current_token.type == TOKEN_IDENTIFIER) {
        fn->fn_name = strdup(parser->current_token.lexeme);
        ffi_advance(parser);
    }

    if (parser->current_token.type == TOKEN_LPAREN) {
        ffi_advance(parser); // Consume '('

        int cap = 4;
        fn->params = (FFIFnParam*)malloc(sizeof(FFIFnParam) * cap);
        fn->param_count = 0;

        while (parser->current_token.type != TOKEN_RPAREN && parser->current_token.type != TOKEN_EOF) {
            if (parser->current_token.type == TOKEN_IDENTIFIER) {
                if (fn->param_count >= cap) {
                    cap *= 2;
                    fn->params = (FFIFnParam*)realloc(fn->params, sizeof(FFIFnParam) * cap);
                }

                fn->params[fn->param_count].param_name = strdup(parser->current_token.lexeme);
                ffi_advance(parser); // Consume the parameter name

                if (parser->current_token.type == TOKEN_COLON) {
                    ffi_advance(parser); // Consume ':'
                    fn->params[fn->param_count].param_type = parse_complex_type(parser);
                }
                fn->param_count++;
            }

            if (parser->current_token.type == TOKEN_COMMA) {
                ffi_advance(parser); // Consume ','
            }
        }
        if (parser->current_token.type == TOKEN_RPAREN) ffi_advance(parser); // Consume ')'
    }

    // Capture the return type ': type'
    if (parser->current_token.type == TOKEN_COLON) {
        ffi_advance(parser); // Consume ':'
        fn->return_type = parse_complex_type(parser);
    } else {
        fn->return_type = strdup("void");
    }

    fn->alias = NULL;
}

// Entry point to parse any 'extern' block
FFIASTNode* parse_extern_declaration(Parser* parser) {
    ffi_advance(parser); // Consume 'extern'

    FFIASTNode* node = (FFIASTNode*)malloc(sizeof(FFIASTNode));
    node->from_lib = NULL;

    // NEW CASE: extern """ native C code """ as { ... }
    if (parser->current_token.type == TOKEN_LIT_MULTILINE_STR) {
        node->type = NODE_FFI_INLINE_C;
        node->data.ffi_inline_c.c_code_block = strdup(parser->current_token.lexeme);
        ffi_advance(parser); // Consume the multiline string block

        // Validate and consume the mandatory 'as' keyword
        if (parser->current_token.type == TOKEN_AS) {
            ffi_advance(parser); // Consume 'as'
        } else {
            fprintf(stderr, "[Syntax Error] Line %d: Expected 'as' after the extern native code block\n", parser->current_token.line);
        }

        // Process the Teko function mapping scope '{ ... }'
        if (parser->current_token.type == TOKEN_LBRACE) {
            ffi_advance(parser); // Consume '{'

            int cap = 4;
            node->data.ffi_inline_c.declarations = (FFIASTNode**)malloc(sizeof(FFIASTNode*) * cap);
            node->data.ffi_inline_c.declaration_count = 0;

            while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
                if (parser->current_token.type == TOKEN_FN) {
                    if (node->data.ffi_inline_c.declaration_count >= cap) {
                        cap *= 2;
                        node->data.ffi_inline_c.declarations = (FFIASTNode**)realloc(
                            node->data.ffi_inline_c.declarations, sizeof(FFIASTNode*) * cap);
                    }

                    FFIASTNode* inner_fn = (FFIASTNode*)malloc(sizeof(FFIASTNode));
                    inner_fn->type = NODE_FFI_FUNCTION;
                    inner_fn->from_lib = NULL;

                    // Reuse the signature already implemented earlier
                    parse_single_function_signature(parser, &inner_fn->data.ffi_function);

                    if (parser->current_token.type == TOKEN_SEMICOLON) {
                        ffi_advance(parser);
                    }

                    node->data.ffi_inline_c.declarations[node->data.ffi_inline_c.declaration_count++] = inner_fn;
                } else {
                    ffi_advance(parser); // Cleanup if something out of spec is typed
                }
            }

            if (parser->current_token.type == TOKEN_RBRACE) {
                ffi_advance(parser); // Consume '}'
            }
        }
        return node;
    }

    // --- BELOW THIS POINT FOLLOWS THE ORIGINAL PRIOR FLOW, COMPACTED ---
    if (parser->current_token.type == TOKEN_STRUCT) {
        node->type = NODE_FFI_STRUCT;
        ffi_advance(parser);
        if (parser->current_token.type == TOKEN_IDENTIFIER) {
            node->data.ffi_struct.struct_name = strdup(parser->current_token.lexeme);
            ffi_advance(parser);
        }
        if (parser->current_token.type == TOKEN_LBRACE) {
            ffi_advance(parser);
            int cap = 4;
            node->data.ffi_struct.fields = (FFIStructField*)malloc(sizeof(FFIStructField) * cap);
            node->data.ffi_struct.field_count = 0;
            while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
                if (parser->current_token.type == TOKEN_IDENTIFIER) {
                    if (node->data.ffi_struct.field_count >= cap) {
                        cap *= 2;
                        node->data.ffi_struct.fields = (FFIStructField*)realloc(node->data.ffi_struct.fields, sizeof(FFIStructField) * cap);
                    }
                    node->data.ffi_struct.fields[node->data.ffi_struct.field_count].field_name = strdup(parser->current_token.lexeme);
                    ffi_advance(parser);
                    if (parser->current_token.type == TOKEN_COLON) {
                        ffi_advance(parser);
                        node->data.ffi_struct.fields[node->data.ffi_struct.field_count].field_type = parse_complex_type(parser);
                    }
                    node->data.ffi_struct.field_count++;
                }
                if (parser->current_token.type == TOKEN_SEMICOLON) ffi_advance(parser);
            }
            if (parser->current_token.type == TOKEN_RBRACE) ffi_advance(parser);
        }
    } else if (parser->current_token.type == TOKEN_FN) {
        node->type = NODE_FFI_FUNCTION;
        parse_single_function_signature(parser, &node->data.ffi_function);
    } else if (parser->current_token.type == TOKEN_LBRACE) {
        node->type = NODE_FFI_BLOCK;
        ffi_advance(parser);
        int cap = 4;
        node->data.ffi_block.functions = (FFIASTNode**)malloc(sizeof(FFIASTNode*) * cap);
        node->data.ffi_block.function_count = 0;
        while (parser->current_token.type != TOKEN_RBRACE && parser->current_token.type != TOKEN_EOF) {
            if (parser->current_token.type == TOKEN_FN) {
                if (node->data.ffi_block.function_count >= cap) {
                    cap *= 2;
                    node->data.ffi_block.functions = (FFIASTNode**)realloc(node->data.ffi_block.functions, sizeof(FFIASTNode*) * cap);
                }
                FFIASTNode* inner_fn = (FFIASTNode*)malloc(sizeof(FFIASTNode));
                inner_fn->type = NODE_FFI_FUNCTION;
                inner_fn->from_lib = NULL;
                parse_single_function_signature(parser, &inner_fn->data.ffi_function);
                if (parser->current_token.type == TOKEN_SEMICOLON) ffi_advance(parser);
                node->data.ffi_block.functions[node->data.ffi_block.function_count++] = inner_fn;
            } else {
                ffi_advance(parser);
            }
        }
        if (parser->current_token.type == TOKEN_RBRACE) ffi_advance(parser);
    }

    if (parser->current_token.type == TOKEN_FROM) {
        ffi_advance(parser);
        if (parser->current_token.type == TOKEN_LIT_STR) {
            node->from_lib = strdup(parser->current_token.lexeme);
            ffi_advance(parser);
        }
    }

    if (parser->current_token.type == TOKEN_AS) {
        ffi_advance(parser);
        if (parser->current_token.type == TOKEN_LIT_STR && node->type == NODE_FFI_FUNCTION) {
            node->data.ffi_function.alias = strdup(parser->current_token.lexeme);
            ffi_advance(parser);
        }
    }

    if (parser->current_token.type == TOKEN_SEMICOLON) {
        ffi_advance(parser);
    }

    return node;
}

// Recursive memory cleanup for the FFI subsystem
void free_ffi_ast_node(FFIASTNode* node) {
    if (!node) return;
    if (node->from_lib) {
        free(node->from_lib);
        node->from_lib = NULL;
    }

    if (node->type == NODE_FFI_STRUCT) {
        if (node->data.ffi_struct.struct_name) {
            free(node->data.ffi_struct.struct_name);
        }
        if (node->data.ffi_struct.fields) {
            for (int i = 0; i < node->data.ffi_struct.field_count; i++) {
                if (node->data.ffi_struct.fields[i].field_name) {
                    free(node->data.ffi_struct.fields[i].field_name);
                }
                if (node->data.ffi_struct.fields[i].field_type) {
                    free(node->data.ffi_struct.fields[i].field_type);
                }
            }
            free(node->data.ffi_struct.fields);
        }
    } else if (node->type == NODE_FFI_FUNCTION) {
        if (node->data.ffi_function.fn_name) {
            free(node->data.ffi_function.fn_name);
        }
        if (node->data.ffi_function.return_type) {
            free(node->data.ffi_function.return_type);
        }
        if (node->data.ffi_function.alias) {
            free(node->data.ffi_function.alias);
        }
        if (node->data.ffi_function.params) {
            for (int i = 0; i < node->data.ffi_function.param_count; i++) {
                if (node->data.ffi_function.params[i].param_name) {
                    free(node->data.ffi_function.params[i].param_name);
                }
                if (node->data.ffi_function.params[i].param_type) {
                    free(node->data.ffi_function.params[i].param_type);
                }
            }
            free(node->data.ffi_function.params);
        }
    } else if (node->type == NODE_FFI_BLOCK) {
        if (node->data.ffi_block.functions) {
            for (int i = 0; i < node->data.ffi_block.function_count; i++) {
                if (node->data.ffi_block.functions[i]) {
                    free_ffi_ast_node(node->data.ffi_block.functions[i]);
                }
            }
            free(node->data.ffi_block.functions);
        }
    } else if (node->type == NODE_FFI_INLINE_C) {
        if (node->data.ffi_inline_c.c_code_block) {
            free(node->data.ffi_inline_c.c_code_block);
        }
        if (node->data.ffi_inline_c.declarations) {
            for (int i = 0; i < node->data.ffi_inline_c.declaration_count; i++) {
                if (node->data.ffi_inline_c.declarations[i]) {
                    free_ffi_ast_node(node->data.ffi_inline_c.declarations[i]);
                }
            }
            free(node->data.ffi_inline_c.declarations);
        }
    }
    free(node);
}