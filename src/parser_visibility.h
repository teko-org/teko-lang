#ifndef PARSER_VISIBILITY_H
#define PARSER_VISIBILITY_H

#include "parser.h"

// Language visibility and encapsulation levels
typedef enum {
    VIS_INTERNAL = 0, // Default visibility (omitted modifier)
    VIS_PROJECT_PUB,  // 'pub' modifier (visible within the project)
    VIS_EXPORTED_EXP  // 'exp' modifier (exported outside)
} VisibilityKind;

// Specific AST nodes for declarations with expanded visibility scope
typedef enum {
    NODE_VISIBLE_DECL = 1100
} VisibilityASTNodeType;

// Wrapper node that associates a visibility level with any global declaration
typedef struct VisibilityASTNode {
    int type;                  // NODE_VISIBLE_DECL
    VisibilityKind visibility; // Modifier kind
    void* decorated_node;      // Points to the real node (struct, function, interface, etc.)
    int decorated_node_type;   // Stores the original type of the decorated node for codegen
} VisibilityASTNode;

// Public signatures for the Visibility parser
VisibilityASTNode* parse_global_declaration_with_visibility(Parser* parser);
void free_visibility_ast_node(VisibilityASTNode* node);

#endif // PARSER_VISIBILITY_H