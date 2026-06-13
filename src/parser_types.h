#ifndef PARSER_TYPES_H
#define PARSER_TYPES_H

#include "parser.h"

typedef enum {
    NODE_TYPE_BASIC = 300,   // i32, u8, str, char, etc.
    NODE_TYPE_DECIMAL,       // decimal up to 256 bits
    NODE_TYPE_BIGINT,        // bigint
    NODE_TYPE_ARENA,         // arena
    NODE_ARBITRARY_LITERAL,  // arbitrary decimal and bigint
    NODE_TYPE_GENERIC,       // E.g.: intent<u8> or list<mut dec>
    NODE_TYPE_FUNC           // E.g.: func<i32, void>
} ArbitraryTypeASTNodeType;

// Structure representing a complete, complex type in the AST
typedef struct TypeInfo {
    int kind;                 // ArbitraryTypeASTNodeType
    char* base_name;          // Base type name (e.g.: "intent", "i32", "map")
    bool is_nullable;         // true if '?' is present (Elvis notation)
    bool is_array;            // true if '[]' is present
    bool is_array_elem_mut;   // true if the array element is 'mut' (e.g.: mut dec[])
    char* file_mode;

    // Generic type parameters or func<> arguments
    struct TypeInfo** generic_params;
    int generic_param_count;
} TypeInfo;

// AST node for literals and declarations
typedef struct ArbitraryTypeASTNode {
    int type;
    union {
        struct {
            char* raw_lexeme;
            bool is_floating_point;
        } numeric_literal;

        struct {
            TypeInfo* type_info;
            char* constraint_target; // For 'where T : struct' clauses
            char* constraint_bound;  // The constraint type (e.g.: "struct")
        } type_decl;
    } data;
} ArbitraryTypeASTNode;

// Updated public signatures
TypeInfo* parse_complete_type_info(Parser* parser);
ArbitraryTypeASTNode* parse_arbitrary_numeric_literal(Parser* parser);
void free_type_info(TypeInfo* info);
void free_arbitrary_type_ast_node(ArbitraryTypeASTNode* node);

#endif // PARSER_TYPES_H