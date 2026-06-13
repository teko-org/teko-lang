#ifndef PARSER_GENERICS_H
#define PARSER_GENERICS_H

#include "parser.h"
#include "parser_types.h"

// Specific nodes for generic type constraints and parameters
typedef enum {
    NODE_GENERIC_CONSTRAINT = 800,
    NODE_GENERIC_PARAM_DECL
} GenericASTNodeType;

// Structure representing a C#-style constraint
typedef struct GenericConstraint {
    char* type_parameter_name; // The constrained parameter (e.g.: "T")
    char* constraint_bound;    // The bound rule/type (e.g.: "struct")
} GenericConstraint;

// Unified function signature structure with full generics support
typedef struct GenericFunctionSignature {
    char* fn_name;
    TypeInfo* return_type;
    bool is_async;

    // Function type parameters (e.g.: <T> or <T, U>)
    char** type_parameters;
    int type_parameter_count;

    // Associated 'where' clauses
    GenericConstraint* constraints;
    int constraint_count;
} GenericFunctionSignature;

// Public signatures for the Generics parser
void parse_generic_parameters_decl(Parser* parser, GenericFunctionSignature* sig);
void parse_generic_constraints_where(Parser* parser, GenericFunctionSignature* sig);
void free_generic_function_signature_members(GenericFunctionSignature* sig);

#endif // PARSER_GENERICS_H