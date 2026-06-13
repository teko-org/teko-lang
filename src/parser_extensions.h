#ifndef PARSER_EXTENSIONS_H
#define PARSER_EXTENSIONS_H

#include "parser.h"
#include "parser_types.h"

// Specific nodes for extensions and inline operators in the AST
typedef enum {
    NODE_TYPE_EXTENSION = 700,
    NODE_EXTENSION_METHOD,
    NODE_EXTENSION_OPERATOR
} ExtensionASTNodeType;

// Structure describing a method or operator inside an extend block
typedef struct ExtensionMemberNode {
    int type;                // NODE_EXTENSION_METHOD or NODE_EXTENSION_OPERATOR
    char* name;              // Method name or operator symbol (e.g.: "+")
    TypeInfo* return_type;   // Return type
    char* param_name;        // Parameter name (for operators)
    TypeInfo* param_type;    // Parameter type (for operators)
    bool is_inline;          // true if defined with '=>'
    char* body_raw;          // Body content or inline expression for the code generator
} ExtensionMemberNode;

// Main AST node for the extend block
typedef struct ExtensionASTNode {
    int type;                // NODE_TYPE_EXTENSION
    char* self_param_name;   // E.g.: "self"
    TypeInfo* self_type;     // The type being extended (e.g.: "string" or "str")
    ExtensionMemberNode* members;
    int member_count;
} ExtensionASTNode;

// Public signatures for the Extensions parser
ExtensionASTNode* parse_type_extension(Parser* parser);
void free_extension_ast_node(ExtensionASTNode* node);

#endif // PARSER_EXTENSIONS_H