#ifndef PARSER_FFI_H
#define PARSER_FFI_H

#include "parser.h"

// Extending AST node types to support FFI
typedef enum {
    NODE_FFI_STRUCT = 100, // Avoids collision with prior node values
    NODE_FFI_FUNCTION,
    NODE_FFI_BLOCK,
    NODE_FFI_INLINE_C,
    NODE_STRUCT_FIELD,
    NODE_FN_PARAM
} FFIASTNodeType;

// Structure for FFI struct fields (e.g.: name: str;)
typedef struct FFIStructField {
    char* field_name;
    char* field_type;
} FFIStructField;

// Structure for external function parameters (e.g.: p: ptr<ExternalStructure>)
typedef struct FFIFnParam {
    char* param_name;
    char* param_type;
} FFIFnParam;

// Structure for an external function node
typedef struct FFIFunctionNode {
    char* fn_name;
    char* return_type;
    char* alias;             // Populated when 'as "Alias"' is present
    FFIFnParam* params;
    int param_count;
} FFIFunctionNode;

// Unified FFI node structure for the AST
typedef struct FFIASTNode {
    int type; // Receives values from the FFIASTNodeType enum
    char* from_lib; // Library path (e.g.: "my.dylib", "another.so")
    union {
        // Data for: extern struct Name { ... }
        struct {
            char* struct_name;
            FFIStructField* fields;
            int field_count;
        } ffi_struct;

        // Data for: extern fn name() : type
        FFIFunctionNode ffi_function;

        // Data for: extern { fn1(); fn2(); }
        struct {
            struct FFIASTNode** functions;
            int function_count;
        } ffi_block;

        // Data for: extern """ c_code """ as { functions }
        struct {
            char* c_code_block; // Stores the raw text contained between the triple quotes
            struct FFIASTNode** declarations; // Mapping of Teko signatures inside 'as { ... }'
            int declaration_count;
        } ffi_inline_c;
    } data;
} FFIASTNode;

// Function signatures for the FFI parser
FFIASTNode* parse_extern_declaration(Parser* parser);
void free_ffi_ast_node(FFIASTNode* node);

#endif // PARSER_FFI_H